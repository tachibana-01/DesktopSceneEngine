#include "LauncherWindow.h"

#include "DisplaySessionBootstrap.h"
#include "DisplayPresentation.h"

#include <QCoreApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>
#include <QToolButton>

#include <cstddef>
#include <optional>
#include <string>

namespace
{

QString launcherText(const char* sourceText)
{
    return QCoreApplication::translate("LauncherWindow", sourceText);
}

QString enumerationFailureText(
    DesktopSceneEngine::Platform::DisplayEnumerationStatus status
)
{
    using DesktopSceneEngine::Platform::DisplayEnumerationStatus;

    switch (status)
    {
    case DisplayEnumerationStatus::IncompletePathMetadata:
        return launcherText(
            "Display enumeration failed because persistent identity metadata is incomplete."
        );
    case DisplayEnumerationStatus::DuplicateIdentity:
        return launcherText(
            "Display enumeration failed because display identities are ambiguous."
        );
    case DisplayEnumerationStatus::ApiFailure:
        return launcherText("Display enumeration failed.");
    case DisplayEnumerationStatus::Success:
        break;
    }

    return launcherText("Display enumeration failed.");
}

}

namespace DesktopSceneEngine::Player
{

void LauncherWindow::closeEvent(QCloseEvent* event)
{
    QMainWindow::closeEvent(event);
    if (event->isAccepted())
    {
        trayPresentation_.beginTeardown();
        if (acceptedCloseCallback_) acceptedCloseCallback_();
    }
}

LauncherWindow::LauncherWindow(ApplicationTranslationState& translationState,
    ApplicationPreferencesStore& preferencesStore, QWidget* parent)
    : QMainWindow(parent), translationState_(translationState), preferencesStore_(preferencesStore)
{
    setWindowTitle(launcherText("DesktopSceneEngine Launcher"));

    auto* centralWidget = new QWidget(this);
    auto* layout = new QVBoxLayout(centralWidget);
    auto* presentationRow = new QHBoxLayout();
    presentationRow->setContentsMargins(4, 2, 4, 2);
    presentationRow->addStretch();
    hideToTrayButton_ = new QToolButton(centralWidget);
    hideToTrayButton_->setObjectName(QStringLiteral("launcherHideToTray"));
    hideToTrayButton_->setText(QString::fromUtf8("⌄"));
    hideToTrayButton_->setAutoRaise(true);
    hideToTrayButton_->setMinimumSize(28, 24);
    hideToTrayButton_->setToolTip(launcherText("Hide to System Tray"));
    hideToTrayButton_->setAccessibleName(launcherText("Hide to System Tray"));
    presentationRow->addWidget(hideToTrayButton_);
    layout->addLayout(presentationRow);
    trayPresentation_.bindButton(*hideToTrayButton_);
    instructionLabel_ = new QLabel(
        launcherText("Select a display to start an independent Display Session."),
        centralWidget
    );
    instructionLabel_->setWordWrap(true);
    displayList_ = new QListWidget(centralWidget);
    statusLabel_ = new QLabel(centralWidget);
    statusLabel_->setWordWrap(true);
    refreshButton_ = new QPushButton(launcherText("Refresh"), centralWidget);
    identifyButton_ = new QPushButton(launcherText("Identify"), centralWidget);
    startSessionButton_ = new QPushButton(launcherText("Start Session"), centralWidget);
    settingsButton_ = new QPushButton(launcherText("Settings"), centralWidget);

    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->addWidget(refreshButton_);
    actionsLayout->addWidget(settingsButton_);
    actionsLayout->addStretch();
    actionsLayout->addWidget(identifyButton_);
    actionsLayout->addWidget(startSessionButton_);

    layout->addWidget(instructionLabel_);
    layout->addWidget(displayList_, 1);
    layout->addWidget(statusLabel_);
    layout->addLayout(actionsLayout);
    setCentralWidget(centralWidget);
    resize(680, 400);

    connect(displayList_, &QListWidget::currentRowChanged, this, [this]() {
        updateActions();
    });
    connect(refreshButton_, &QPushButton::clicked, this, [this]() {
        refreshDisplays();
    });
    connect(identifyButton_, &QPushButton::clicked, this, [this]() {
        identifySelectedDisplay();
    });
    connect(startSessionButton_, &QPushButton::clicked, this, [this]() {
        startSelectedDisplaySession();
    });
    connect(settingsButton_, &QPushButton::clicked, this, [this] { openSettings(); });

    refreshDisplays();
}

void LauncherWindow::openSettings()
{
    const auto committed = preferencesStore_.reloadLanguage();
    if (!committed.has_value())
    {
        QMessageBox::warning(this, launcherText("Settings"), launcherText("Unable to read the saved language."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(launcherText("Settings"));
    QFormLayout layout(&dialog);
    QComboBox language(&dialog);
    language.addItem(QStringLiteral("English"), static_cast<int>(UiLanguage::English));
    language.addItem(QString::fromUtf8("简体中文"), static_cast<int>(UiLanguage::SimplifiedChinese));
    language.setCurrentIndex(language.findData(static_cast<int>(*committed)));
    layout.addRow(launcherText("Language"), &language);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    // The dialog is closed before committed Language changes the process UI.
    bool valid = false;
    const auto selected = language.currentData().toInt(&valid);
    if (!valid || (selected != static_cast<int>(UiLanguage::English)
        && selected != static_cast<int>(UiLanguage::SimplifiedChinese))) return;
    commitLanguage(static_cast<UiLanguage>(selected));
}

void LauncherWindow::commitLanguage(UiLanguage language, bool (&publish)())
{
    const auto committed = preferencesStore_.reloadLanguage();
    if (!committed.has_value())
    {
        QMessageBox::warning(this, launcherText("Settings"), launcherText("Unable to read the saved language."));
        return;
    }
    if (*committed == language) return;
    if (preferencesStore_.saveLanguage(language) != QSettings::NoError)
    {
        QMessageBox::warning(this, launcherText("Settings"),
            launcherText("Unable to save language. No language change was applied."));
        return;
    }
    const auto effective = translationState_.setLanguage(language);
    retranslateUi();
    const bool published = publish();
    QStringList issues;
    if (effective != language)
        issues.push_back(launcherText("Language was saved, but its translation catalog could not be applied here."));
    if (!published)
        issues.push_back(launcherText("Language was saved, but live notification could not be published. New sessions will use the saved language."));
    if (!issues.isEmpty()) QMessageBox::warning(this, launcherText("Settings"), issues.join(QLatin1Char('\n')));
}

void LauncherWindow::retranslateUi()
{
    setWindowTitle(launcherText("DesktopSceneEngine Launcher"));
    instructionLabel_->setText(launcherText("Select a display to start an independent Display Session."));
    refreshButton_->setText(launcherText("Refresh"));
    settingsButton_->setText(launcherText("Settings"));
    identifyButton_->setText(launcherText("Identify"));
    startSessionButton_->setText(launcherText("Start Session"));
    hideToTrayButton_->setToolTip(launcherText("Hide to System Tray"));
    hideToTrayButton_->setAccessibleName(launcherText("Hide to System Tray"));
    for (int row = 0; row < displayList_->count(); ++row)
        displayList_->item(row)->setText(displayLabel(displayEnumeration_.snapshot.displays[static_cast<std::size_t>(row)]));
    updateStatus();
}

void LauncherWindow::updateStatus()
{
    if (statusSourceText_) statusLabel_->setText(launcherText(statusSourceText_));
    else if (displayEnumeration_.status != Platform::DisplayEnumerationStatus::Success)
        statusLabel_->setText(enumerationFailureText(displayEnumeration_.status));
    else if (rowDisplayIds_.empty()) statusLabel_->setText(launcherText("No active displays were found."));
    else statusLabel_->setText(launcherText("Active displays: %1").arg(rowDisplayIds_.size()));
}

void LauncherWindow::refreshDisplays()
{
    applyDisplayEnumeration(Platform::enumerateActiveDisplays());
}

void LauncherWindow::applyDisplayEnumeration(
    const Platform::DisplayEnumerationResult& result
)
{
    const std::optional<DisplayId> previousSelection = selectedDisplayId();
    displayEnumeration_ = result;
    statusSourceText_ = nullptr;
    const QSignalBlocker signalBlocker(displayList_);
    displayList_->clear();
    rowDisplayIds_.clear();

    if (displayEnumeration_.status != Platform::DisplayEnumerationStatus::Success)
    {
        updateStatus();
        updateActions();
        return;
    }

    int selectedRow = -1;
    rowDisplayIds_.reserve(displayEnumeration_.snapshot.displays.size());
    for (const Platform::DisplayDescriptor& display : displayEnumeration_.snapshot.displays)
    {
        const int row = static_cast<int>(rowDisplayIds_.size());
        rowDisplayIds_.push_back(display.id);
        displayList_->addItem(displayLabel(display));
        if (previousSelection.has_value() && display.id == *previousSelection)
        {
            selectedRow = row;
        }
    }

    displayList_->setCurrentRow(selectedRow);
    updateStatus();

    updateActions();
}

void LauncherWindow::identifySelectedDisplay()
{
    const std::optional<DisplayId> selected = selectedDisplayId();
    if (!selected.has_value())
    {
        return;
    }

    const Platform::DisplayIdentifyStatus status = Platform::identifyDisplay(*selected, 2500U);
    switch (status)
    {
    case Platform::DisplayIdentifyStatus::Shown:
        statusSourceText_ = "Display identification is being shown.";
        updateStatus();
        return;
    case Platform::DisplayIdentifyStatus::Unresolved:
        statusSourceText_ = "The selected display is no longer available.";
        updateStatus();
        return;
    case Platform::DisplayIdentifyStatus::InvalidRequest:
    case Platform::DisplayIdentifyStatus::ApiFailure:
        statusSourceText_ = "Unable to identify the selected display.";
        updateStatus();
        return;
    }
}

void LauncherWindow::startSelectedDisplaySession()
{
    const std::optional<DisplayId> selected = selectedDisplayId();
    if (!selected.has_value() || !selected->isPersistentOwnerCapable())
    {
        statusSourceText_ = "The selected display cannot own a Display Session.";
        updateStatus();
        return;
    }

    if (!startDisplaySessionDetached(*selected))
    {
        statusSourceText_ = "Unable to start the Display Session.";
        updateStatus();
        return;
    }

    statusSourceText_ = "Display Session started.";
    updateStatus();
}

void LauncherWindow::updateActions()
{
    const std::optional<DisplayId> selected = selectedDisplayId();
    identifyButton_->setEnabled(selected.has_value() && selected->isValid());
    startSessionButton_->setEnabled(
        selected.has_value()
            && selected->isValid()
            && selected->isPersistentOwnerCapable()
    );
}

std::optional<DisplayId> LauncherWindow::selectedDisplayId() const
{
    const int row = displayList_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= rowDisplayIds_.size())
    {
        return std::nullopt;
    }

    return rowDisplayIds_[static_cast<std::size_t>(row)];
}

}
