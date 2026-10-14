#include "MainWindow.h"
#include "GlobalSettingsNotification.h"

#include "BackgroundMusicControlAdapter.h"
#include "BackgroundMusicWindow.h"
#include "CanvasChromeFrame.h"
#include "EffectiveVolume.h"
#include "PreviewViewportHost.h"
#include "TimelineHoverGeometry.h"
#include "TimelinePreviewPresentation.h"
#include "TimelineRepresentativeTimestamp.h"
#include "TimelineUtils.h"

#include "Infrastructure/Persistence/ProjectFile.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QThread>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{

using DesktopSceneEngine::Player::ItemId;
using DesktopSceneEngine::Player::PlaybackSequenceMode;
using DesktopSceneEngine::Player::PlaylistId;
using DesktopSceneEngine::Player::TrackId;
using DesktopSceneEngine::Player::TimelinePreviewSource;
using DesktopSceneEngine::Player::WorkspaceMediaType;
using DesktopSceneEngine::CanvasOutsideMode;

constexpr int workspaceIdRole = Qt::UserRole;
constexpr std::uint32_t IdentifyDurationMilliseconds = 2500U;
constexpr std::size_t MaximumRandomRoundResolutionAttempts = 2U;

[[nodiscard]] DesktopSceneEngine::Domain::DisplayOwner requireDisplayOwner(
    const DesktopSceneEngine::DisplayId& displayId)
{
    auto owner = DesktopSceneEngine::Domain::DisplayOwner::fromDisplayId(displayId);
    if (!owner.has_value())
    {
        throw std::invalid_argument{
            "MainWindow requires a persistent-owner-capable DisplayId."};
    }

    return std::move(*owner);
}

[[nodiscard]] std::uint64_t playbackSequenceSeed()
{
    std::random_device entropy{};
    return (static_cast<std::uint64_t>(entropy()) << 32U)
        ^ static_cast<std::uint64_t>(entropy());
}

[[nodiscard]] PlaylistId playlistIdFor(const QListWidgetItem* item)
{
    return item == nullptr
        ? PlaylistId{}
        : PlaylistId{item->data(workspaceIdRole).toUuid()};
}

[[nodiscard]] ItemId itemIdFor(const QListWidgetItem* item)
{
    return item == nullptr
        ? ItemId{}
        : ItemId{item->data(workspaceIdRole).toUuid()};
}

[[nodiscard]] QString displayAlias(const QString& alias, bool active)
{
    return active ? QString(QChar{0x25B6}) + QStringLiteral(" ") + alias : alias;
}

[[nodiscard]] bool sameBackgroundMusicScope(
    const DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot& left,
    const DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot& right
) noexcept
{
    return left.playlistId == right.playlistId && left.itemId == right.itemId;
}

[[nodiscard]] std::optional<TrackId> workspaceTrackIdFor(
    const DesktopSceneEngine::Player::MusicQueueConfig* queue,
    const std::optional<DesktopSceneEngine::BackgroundMusicControl::TrackId>& trackId)
{
    if (queue == nullptr || !trackId.has_value())
    {
        return std::nullopt;
    }

    for (const DesktopSceneEngine::Player::MusicTrack& track : queue->tracks)
    {
        const auto publicTrackId = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(
            track.id);
        if (publicTrackId.has_value() && *publicTrackId == *trackId)
        {
            return track.id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<WorkspaceMediaType> mediaTypeForSource(
    const QString& source)
{
    const QString extension = QFileInfo{source}.suffix().toLower();
    if (extension == QStringLiteral("png") || extension == QStringLiteral("jpg")
        || extension == QStringLiteral("jpeg") || extension == QStringLiteral("webp"))
    {
        return WorkspaceMediaType::Image;
    }

    if (extension == QStringLiteral("mp4") || extension == QStringLiteral("avi")
        || extension == QStringLiteral("mov") || extension == QStringLiteral("mkv")
        || extension == QStringLiteral("webm") || extension == QStringLiteral("video"))
    {
        return WorkspaceMediaType::Video;
    }

    return std::nullopt;
}

QLabel* sectionHeader(QWidget* parent, const QString& title)
{
    auto* label = new QLabel(title, parent);
    label->setStyleSheet(
        QStringLiteral(
            "QLabel { color: #9aa6b2; font-size: 11px; "
            "font-weight: 600; letter-spacing: 1px; }"));
    return label;
}

void addSeparator(QVBoxLayout* layout, QWidget* parent)
{
    auto* separator = new QFrame(parent);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet(QStringLiteral("color: #303842;"));
    layout->addWidget(separator);
}

void makeHorizontallyShrinkable(QWidget* widget)
{
    widget->setMinimumWidth(0);
    widget->setSizePolicy(
        QSizePolicy::Ignored,
        widget->sizePolicy().verticalPolicy());
}

void addPlaybackModeItems(QComboBox& combo)
{
    combo.addItem({}, static_cast<int>(PlaybackSequenceMode::ListLoop));
    combo.addItem({}, static_cast<int>(PlaybackSequenceMode::Random));
    combo.addItem({}, static_cast<int>(PlaybackSequenceMode::RepeatCurrent));
}

[[nodiscard]] std::optional<PlaybackSequenceMode> playbackModeForComboIndex(
    const QComboBox& combo,
    int index
)
{
    bool converted = false;
    const int value = combo.itemData(index).toInt(&converted);
    if (!converted)
    {
        return std::nullopt;
    }

    const auto mode = static_cast<PlaybackSequenceMode>(value);
    return DesktopSceneEngine::Player::isPlaybackSequenceMode(mode)
        ? std::optional{mode}
        : std::nullopt;
}

void setPlaybackModeText(
    QComboBox& combo,
    PlaybackSequenceMode mode,
    const QString& text
)
{
    const int index = combo.findData(static_cast<int>(mode));
    if (index >= 0)
    {
        combo.setItemText(index, text);
    }
}

[[nodiscard]] DesktopSceneEngine::Player::TimelinePreviewDisplayedFrame::Identity
timelinePreviewIdentity(
    const DesktopSceneEngine::Player::TimelinePreviewSource& source,
    float timeSeconds,
    std::uint64_t generation,
    std::uint64_t serial,
    std::optional<std::int64_t> representativeTimestampMilliseconds = std::nullopt
)
{
    return {source,
            timeSeconds,
            generation,
            serial,
            representativeTimestampMilliseconds};
}

[[nodiscard]] std::filesystem::path timelinePreviewMediaCacheRoot()
{
    return std::filesystem::path{QDir{QCoreApplication::applicationDirPath()}
        .filePath(QStringLiteral("Cache/Media"))
        .toStdWString()};
}

[[nodiscard]] std::optional<std::int64_t> durationMilliseconds(float seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds <= 0.0F)
    {
        return std::nullopt;
    }

    const double milliseconds = static_cast<double>(seconds) * 1'000.0;
    if (milliseconds > static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
    {
        return std::nullopt;
    }

    const std::int64_t roundedMilliseconds = static_cast<std::int64_t>(std::llround(milliseconds));
    return roundedMilliseconds > 0 ? std::optional{roundedMilliseconds} : std::nullopt;
}

}

class MainWindow::RenderWidget final : public QWidget
{
public:

    explicit RenderWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAutoFillBackground(false);
        setMinimumSize(320, 240);
        setStyleSheet(QStringLiteral("background-color: #080b0f;"));
    }
};

class MainWindow::TimelineHoverPopup final : public QFrame
{
public:

    explicit TimelineHoverPopup(QWidget* parent)
        : QFrame(parent)
    {
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
        setFrameShape(QFrame::StyledPanel);
        setStyleSheet(QStringLiteral(
            "QFrame { background-color: #171c23; border: 1px solid #3a4350; border-radius: 4px; }"
            "QLabel { color: #e7edf4; }"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);

        imageLabel_ = new QLabel(this);
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setStyleSheet(QStringLiteral("background-color: #0b0f14;"));
        layout->addWidget(imageLabel_);

        timestampLabel_ = new QLabel(this);
        timestampLabel_->setAlignment(Qt::AlignCenter);
        timestampLabel_->setFixedHeight(20);
        timestampLabel_->setStyleSheet(QStringLiteral("font-weight: 600;"));
        layout->addWidget(timestampLabel_);

        setCanvasSize(QSize{224, 126});
    }

    void setTimestamp(const QString& timestamp)
    {
        timestampLabel_->setText(timestamp);
    }

    void setUnavailableText(const QString& text)
    {
        unavailableText_ = text;
        if (visualState_ == VisualState::Unavailable)
        {
            imageLabel_->setText(unavailableText_);
        }
    }

    void setPendingText(const QString& text)
    {
        pendingText_ = text;
        if (visualState_ == VisualState::Pending)
        {
            imageLabel_->setText(pendingText_);
        }
    }

    void setCanvasSize(const QSize& size)
    {
        if (size.width() <= 0 || size.height() <= 0)
        {
            return;
        }

        imageLabel_->setFixedSize(size);
        setFixedSize(size.width() + 16, size.height() + 40);
    }

    void setPending()
    {
        visualState_ = VisualState::Pending;
        imageLabel_->setPixmap(QPixmap{});
        imageLabel_->setText(pendingText_);
    }

    void setUnavailable(const QString& text)
    {
        unavailableText_ = text;
        visualState_ = VisualState::Unavailable;
        imageLabel_->setPixmap(QPixmap{});
        imageLabel_->setText(unavailableText_);
    }

    void setImage(const QImage& image)
    {
        if (image.isNull())
        {
            setUnavailable(unavailableText_);
            return;
        }

        visualState_ = VisualState::Valid;
        imageLabel_->setText({});
        imageLabel_->setPixmap(QPixmap::fromImage(image).scaled(
            imageLabel_->size(),
            Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation));
    }

private:

    enum class VisualState
    {
        Pending,
        Unavailable,
        Valid
    };

    QLabel* imageLabel_ = nullptr;
    QLabel* timestampLabel_ = nullptr;
    QString unavailableText_{};
    QString pendingText_{};
    VisualState visualState_ = VisualState::Unavailable;
};

MainWindow::MainWindow(
    DesktopSceneEngine::DisplayId boundDisplayId,
    DesktopSceneEngine::Player::ApplicationTranslationState& translationState,
    QWidget* parent
)
    : MainWindow(
          std::move(boundDisplayId),
          translationState,
          static_cast<DesktopSceneEngine::Player::ApplicationPreferencesStore*>(nullptr),
          DesktopSceneEngine::Player::ApplicationPreferences{},
          parent)
{
}

MainWindow::MainWindow(
    DesktopSceneEngine::DisplayId boundDisplayId,
    DesktopSceneEngine::Player::ApplicationTranslationState& translationState,
    DesktopSceneEngine::Player::ApplicationPreferencesStore& preferencesStore,
    DesktopSceneEngine::Player::ApplicationPreferences initialPreferences,
    QWidget* parent
)
    : MainWindow(
          std::move(boundDisplayId),
          translationState,
          &preferencesStore,
          initialPreferences,
          parent)
{
}

MainWindow::MainWindow(
    DesktopSceneEngine::DisplayId boundDisplayId,
    DesktopSceneEngine::Player::ApplicationTranslationState& translationState,
    DesktopSceneEngine::Player::ApplicationPreferencesStore* preferencesStore,
    DesktopSceneEngine::Player::ApplicationPreferences initialPreferences,
    QWidget* parent
)
    : QMainWindow(parent),
      translationState_(translationState),
      preferencesStore_(preferencesStore),
      applicationPreferences_(initialPreferences),
      workspaceModel_(requireDisplayOwner(boundDisplayId)),
      playbackSequence_(playbackSequenceSeed()),
      displaySelection_(std::move(boundDisplayId)),
      desktopReconciliationContinuationTimer_(this),
      tickTimer_(this),
      timelineHoverPreviewTimer_(this)
{
    if (preferencesStore_ != nullptr)
    {
        const auto restored = preferencesStore_->loadCanvasOutsideMode(
            displaySelection_.boundDisplayId());
        currentCanvasOutsideMode_ = restored.value.value_or(CanvasOutsideMode::Opaque);
        if (restored.status != QSettings::NoError)
            std::cerr << "[Player] Desktop background preference read failed: status="
                      << restored.status << std::endl;
    }
    resetWorkspaceForNewDocument();
    documentState_.currentPath.reset();

    setMinimumSize(1000, 650);
    resize(1280, 720);

    createWorkspace();
    createMenus();
    createTimelineHoverPopup();
    statusBar()->setSizeGripEnabled(true);
    applyApplicationPreferences();
    retranslateUi();
    documentState_.markClean(workspaceModel_.authoredRevision());
    workspaceModel_.setAuthoredChangedCallback([this]
    {
        updateDocumentTitle();
    });
    updateDocumentTitle();

    QObject::connect(
        playPauseButton_,
        &QPushButton::clicked,
        this,
        [this]
        {
            togglePlaybackPause();
        });
    QObject::connect(
        muteButton_,
        &QPushButton::clicked,
        this,
        [this]
        {
            engine_.toggleMute();
            refreshStatus();
        });
    QObject::connect(
        volumeSlider_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            const PlaylistId playlistId = volumeControlPlaylistId();
            if (!playlistId.isValid()
                || !workspaceModel_.setPlaylistMasterVolume(
                    playlistId,
                    static_cast<double>(value) / 100.0))
            {
                return;
            }

            synchronizePlaylistInspectorMasterVolume(playlistId);
            updateEffectiveOutputVolume();
            refreshStatus();
        });
    QObject::connect(
        &tickTimer_,
        &QTimer::timeout,
        this,
        [this]
        {
            onTick();
        });
    desktopReconciliationContinuationTimer_.setSingleShot(true);
    desktopReconciliationContinuationTimer_.setInterval(250);
    QObject::connect(
        &desktopReconciliationContinuationTimer_,
        &QTimer::timeout,
        this,
        [this]
        {
            runDesktopPresentationContinuation();
        });
    desktopHost_.setExternalDestructionCallback([this]
    {
        requestDesktopPresentationReconciliation();
    });

#ifdef _WIN32
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreatedMessage_ == 0U)
        std::cerr << "[Player] TaskbarCreated registration failed; Desktop restart notification unavailable.\n";
    globalSettingsChangedMessage_ = DesktopSceneEngine::Player::globalSettingsChangedMessageId();
    if (globalSettingsChangedMessage_ == 0U)
        std::cerr << "[Player] Global Settings invalidation registration failed; live Language updates unavailable.\n";
    // A hidden native MainWindow is the endpoint, independently of presentation
    // targets. Reconcile only after it exists, before the caller can show UI.
    (void)winId();
#endif
    reloadCommittedLanguage();
}

MainWindow::~MainWindow()
{
    trayPresentation_.beginTeardown();
    workspaceModel_.setAuthoredChangedCallback({});
    shutdownEngine();
    closeAllBackgroundMusicWindows();
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef _WIN32
    if (eventType == "windows_generic_MSG" && message != nullptr)
    {
        const auto* native = static_cast<const MSG*>(message);
        if (globalSettingsChangedMessage_ != 0U && native->message == globalSettingsChangedMessage_)
        {
            QMetaObject::invokeMethod(this, [this] { reloadCommittedLanguage(); }, Qt::QueuedConnection);
            if (result) *result = 0;
            return true;
        }
        if (engineInitialized_
            && (native->message == WM_DISPLAYCHANGE
                || (taskbarCreatedMessage_ != 0U && native->message == taskbarCreatedMessage_)))
        {
            requestDesktopPresentationReconciliation();
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::StatusTip)
    {
        event->accept();
        return true;
    }

    const bool handled = QMainWindow::event(event);
    if (event != nullptr && event->type() == QEvent::Resize)
    {
        updateTimelineHoverPopupGeometry();
    }

    return handled;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event == nullptr)
    {
        return QMainWindow::eventFilter(watched, event);
    }

    if (watched == positionSlider_)
    {
        switch (event->type())
        {
        case QEvent::Enter:
            updateTimelineHoverPreview(positionSlider_->mapFromGlobal(QCursor::pos()));
            break;
        case QEvent::MouseMove:
        {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->buttons() == Qt::NoButton)
            {
                updateTimelineHoverPreview(mouseEvent->position().toPoint());
            }
            else
            {
                hideTimelineHoverPopup();
            }
            break;
        }
        case QEvent::Leave:
            hideTimelineHoverPopup();
            break;
        case QEvent::Move:
        case QEvent::Resize:
            updateTimelineHoverPopupGeometry();
            break;
        default:
            break;
        }
    }
    else if (watched == previewViewportHost_
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
    {
        refreshTimelineHoverPreviewPresentation();
        updateTimelineHoverPopupGeometry();
    }
    else if (watched == activePlaylistNameLabel_
        && (event->type() == QEvent::Resize
            || event->type() == QEvent::FontChange
            || event->type() == QEvent::StyleChange))
    {
        updateActivePlaylistNameElision();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createMenus()
{
    fileMenu_ = menuBar()->addMenu(QString{});
    newProjectAction_ = fileMenu_->addAction(QString{});
    openProjectAction_ = fileMenu_->addAction(QString{});
    saveProjectAction_ = fileMenu_->addAction(QString{});
    saveProjectAsAction_ = fileMenu_->addAction(QString{});
    fileMenu_->addSeparator();
    exitAction_ = fileMenu_->addAction(QString{});
    newProjectAction_->setShortcut(QKeySequence::New);
    openProjectAction_->setShortcut(QKeySequence::Open);
    saveProjectAction_->setShortcut(QKeySequence::Save);
    saveProjectAsAction_->setShortcut(QKeySequence::SaveAs);
    QObject::connect(
        newProjectAction_,
        &QAction::triggered,
        this,
        &MainWindow::requestNewProject);
    QObject::connect(
        openProjectAction_,
        &QAction::triggered,
        this,
        &MainWindow::requestOpenProject);
    QObject::connect(
        saveProjectAction_,
        &QAction::triggered,
        this,
        [this]
        {
            (void)saveCurrentProject();
        });
    QObject::connect(
        saveProjectAsAction_,
        &QAction::triggered,
        this,
        [this]
        {
            (void)saveCurrentProjectAs();
        });
    QObject::connect(exitAction_, &QAction::triggered, this, &QWidget::close);

    projectMenu_ = menuBar()->addMenu(QString{});
    addPlaylistAction_ = projectMenu_->addAction(QString{});
    addMediaAction_ = projectMenu_->addAction(QString{});
    QObject::connect(
        addPlaylistAction_,
        &QAction::triggered,
        this,
        [this]
        {
            addPlaylist();
        });
    QObject::connect(
        addMediaAction_,
        &QAction::triggered,
        this,
        [this]
        {
            addMedia();
        });

    playbackMenu_ = menuBar()->addMenu(QString{});
    previousAction_ = playbackMenu_->addAction(QString{});
    playPauseAction_ = playbackMenu_->addAction(QString{});
    nextAction_ = playbackMenu_->addAction(QString{});
    QObject::connect(
        previousAction_,
        &QAction::triggered,
        this,
        [this]
        {
            navigateScene(true);
        });
    QObject::connect(
        playPauseAction_,
        &QAction::triggered,
        this,
        [this]
        {
            togglePlaybackPause();
        });
    QObject::connect(
        nextAction_,
        &QAction::triggered,
        this,
        [this]
        {
            navigateScene(false);
        });

    viewMenu_ = menuBar()->addMenu(QString{});
    resetLayoutAction_ = viewMenu_->addAction(QString{});
    highlightCanvasBoundaryAction_ = viewMenu_->addAction(QString{});
    highlightCanvasBoundaryAction_->setCheckable(true);
    QObject::connect(
        resetLayoutAction_,
        &QAction::triggered,
        this,
        [this]
        {
            if (workspaceSplitter_ != nullptr)
            {
                workspaceSplitter_->setSizes({230, 700, 300});
            }
        });
    QObject::connect(
        highlightCanvasBoundaryAction_,
        &QAction::toggled,
        this,
        [this](bool highlighted)
        {
            applyHighlightCanvasBoundary(highlighted);
            applicationPreferences_.highlightCanvasBoundary = highlighted;
            if (preferencesStore_ != nullptr)
            {
                preferencesStore_->saveHighlightCanvasBoundary(highlighted);
            }
        });

    desktopBackgroundMenu_ = viewMenu_->addMenu(QString{});
    auto* canvasOutsideGroup = new QActionGroup(this);
    canvasOutsideGroup->setExclusive(true);
    canvasOutsideOpaqueAction_ = desktopBackgroundMenu_->addAction(QString{});
    canvasOutsideTransparentAction_ = desktopBackgroundMenu_->addAction(QString{});
    canvasOutsideOpaqueAction_->setObjectName(QStringLiteral("canvasOutsideOpaqueAction"));
    canvasOutsideTransparentAction_->setObjectName(QStringLiteral("canvasOutsideTransparentAction"));
    for (auto* action : {canvasOutsideOpaqueAction_, canvasOutsideTransparentAction_})
    {
        action->setCheckable(true);
        canvasOutsideGroup->addAction(action);
    }
    QObject::connect(canvasOutsideOpaqueAction_, &QAction::triggered, this, [this]
    {
        selectCanvasOutsideMode(CanvasOutsideMode::Opaque);
    });
    QObject::connect(canvasOutsideTransparentAction_, &QAction::triggered, this, [this]
    {
        selectCanvasOutsideMode(CanvasOutsideMode::Transparent);
    });
    projectCanvasOutsideMode();
}

void MainWindow::requestNewProject()
{
    applyNewProjectDecision(maybeSaveCurrentDocument());
}

void MainWindow::requestOpenProject()
{
    const QString selectedPath = QFileDialog::getOpenFileName(
        this,
        tr("Open Project"),
        QString{},
        tr("DesktopSceneEngine Project (*.json);;All Files (*)"));
    if (selectedPath.isEmpty())
    {
        return;
    }

    (void)openProjectFromPath(std::filesystem::path{selectedPath.toStdWString()});
}

QString MainWindow::currentDocumentDisplayName() const
{
    if (documentState_.currentPath.has_value())
    {
        const std::wstring filename = documentState_.currentPath->filename().wstring();
        if (!filename.empty())
        {
            return QString::fromStdWString(filename);
        }
    }

    return tr("Untitled");
}

void MainWindow::updateDocumentTitle()
{
    setWindowTitle(
        tr("DesktopSceneEngine \u2014 %1[*]").arg(currentDocumentDisplayName()));
    setWindowModified(
        documentState_.isDirty(workspaceModel_.authoredRevision()));
}

MainWindow::UnsavedChangesChoice MainWindow::promptForUnsavedChanges()
{
    trayPresentation_.surfaceCloseInteraction(); // Only a tray Close that actually needs interaction surfaces its hidden owner.
    const QMessageBox::StandardButton button = QMessageBox::warning(
        this,
        tr("Unsaved Changes"),
        tr("Save changes to \"%1\"?").arg(currentDocumentDisplayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    switch (button)
    {
    case QMessageBox::Save:
        return UnsavedChangesChoice::Save;
    case QMessageBox::Discard:
        return UnsavedChangesChoice::Discard;
    default:
        return UnsavedChangesChoice::Cancel;
    }
}

MainWindow::DestructiveDecision MainWindow::resolveUnsavedChanges(
    UnsavedChangesChoice choice)
{
    switch (choice)
    {
    case UnsavedChangesChoice::Save:
        return decisionForSaveResult(saveCurrentProject());
    case UnsavedChangesChoice::Discard:
        return DestructiveDecision::ProceedAfterDiscard;
    default:
        return DestructiveDecision::Abort;
    }
}

MainWindow::DestructiveDecision MainWindow::maybeSaveCurrentDocument()
{
    if (!documentState_.isDirty(workspaceModel_.authoredRevision()))
    {
        return DestructiveDecision::ProceedAfterDiscard;
    }

    return resolveUnsavedChanges(promptForUnsavedChanges());
}

MainWindow::DestructiveDecision MainWindow::decisionForSaveResult(
    SaveResult result) noexcept
{
    return result == SaveResult::Saved
        ? DestructiveDecision::ProceedAfterSave
        : DestructiveDecision::Abort;
}

void MainWindow::applyNewProjectDecision(DestructiveDecision decision)
{
    if (decision != DestructiveDecision::Abort)
    {
        createNewDocument();
    }
}

void MainWindow::createNewDocument()
{
    stopActivePlayback();
    closeAllBackgroundMusicWindows();
    resetWorkspaceForNewDocument();
    documentState_.currentPath.reset();
    documentState_.markClean(workspaceModel_.authoredRevision());

    updateCanvasAspectUi();

    refreshLibrary();
    refreshStatus();
    updateDocumentTitle();
}

std::optional<MainWindow::OpenProjectCandidate> MainWindow::loadProjectCandidate(
    const std::filesystem::path& path)
{
    try
    {
        auto loaded = DesktopSceneEngine::Infrastructure::Persistence::
            loadAuthoredProjectFile(path);
        if (loaded.displayOwner.displayId()
            != displaySelection_.boundDisplayId())
        {
            QMessageBox::critical(
                this,
                tr("Open Project Failed"),
                tr("The project belongs to a different display session."));
            return std::nullopt;
        }

        return OpenProjectCandidate{
            std::move(loaded),
            path};
    }
    catch (const std::exception& error)
    {
        QMessageBox::critical(
            this,
            tr("Open Project Failed"),
            tr("Unable to open project:\n%1\n\n%2")
                .arg(QString::fromStdWString(path.wstring()),
                    QString::fromUtf8(error.what())));
        return std::nullopt;
    }
}

bool MainWindow::openProjectFromPath(const std::filesystem::path& path)
{
    auto candidate = loadProjectCandidate(path);
    if (!candidate.has_value())
    {
        return false;
    }

    return completeOpenProject(
        std::move(*candidate),
        maybeSaveCurrentDocument());
}

bool MainWindow::completeOpenProject(
    OpenProjectCandidate candidate,
    DestructiveDecision decision)
{
    if (decision == DestructiveDecision::Abort)
    {
        return false;
    }

    if (decision == DestructiveDecision::ProceedAfterSave)
    {
        auto reloadedCandidate = loadProjectCandidate(candidate.path);
        if (!reloadedCandidate.has_value())
        {
            return false;
        }
        candidate = std::move(*reloadedCandidate);
    }

    commitOpenProject(std::move(candidate));
    return true;
}

void MainWindow::commitOpenProject(OpenProjectCandidate candidate)
{
    stopActivePlayback();
    closeAllBackgroundMusicWindows();
    workspaceModel_.replaceAuthoredProject(std::move(candidate.project));
    documentState_.currentPath = std::move(candidate.path);
    documentState_.markClean(workspaceModel_.authoredRevision());

    updateCanvasAspectUi();

    refreshLibrary();
    refreshStatus();
    updateDocumentTitle();
}

MainWindow::SaveResult MainWindow::saveCurrentProject()
{
    if (!documentState_.currentPath.has_value())
    {
        return saveCurrentProjectAs();
    }

    return saveProjectToPath(*documentState_.currentPath, false);
}

MainWindow::SaveResult MainWindow::saveCurrentProjectAs()
{
    const QString initialPath = documentState_.currentPath.has_value()
        ? QString::fromStdWString(documentState_.currentPath->wstring())
        : QString{};
    QFileDialog dialog{
        this,
        tr("Save Project As"),
        initialPath,
        tr("DesktopSceneEngine Project (*.json);;All Files (*)")};
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(QStringLiteral("json"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
    {
        return SaveResult::Cancelled;
    }

    return saveProjectToPath(
        std::filesystem::path{dialog.selectedFiles().constFirst().toStdWString()},
        true);
}

MainWindow::SaveResult MainWindow::saveProjectToPath(
    const std::filesystem::path& path,
    bool adoptPath)
{
    try
    {
        if (adoptPath)
        {
            DesktopSceneEngine::Infrastructure::Persistence::
                validateAuthoredProjectSaveAsDestination(
                    workspaceModel_.authoredProject().displayOwner,
                    path);
        }
        DesktopSceneEngine::Infrastructure::Persistence::saveAuthoredProjectFile(
            workspaceModel_.authoredProject(),
            path);
    }
    catch (const std::exception& error)
    {
        QMessageBox::critical(
            this,
            tr("Save Project Failed"),
            tr("Unable to save project:\n%1\n\n%2")
                .arg(QString::fromStdWString(path.wstring()),
                    QString::fromUtf8(error.what())));
        return SaveResult::Failed;
    }

    if (adoptPath)
    {
        documentState_.currentPath = path;
    }
    documentState_.markClean(workspaceModel_.authoredRevision());
    updateDocumentTitle();
    return SaveResult::Saved;
}

void MainWindow::createWorkspace()
{
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* presentationRow = new QHBoxLayout();
    presentationRow->setContentsMargins(4, 2, 4, 2);
    presentationRow->addStretch();
    hideToTrayButton_ = new QToolButton(central);
    hideToTrayButton_->setObjectName(QStringLiteral("sessionHideToTray"));
    hideToTrayButton_->setText(QString::fromUtf8("⌄"));
    hideToTrayButton_->setAutoRaise(true);
    hideToTrayButton_->setMinimumSize(28, 24);
    presentationRow->addWidget(hideToTrayButton_);
    layout->addLayout(presentationRow);
    trayPresentation_.bindButton(*hideToTrayButton_);

    workspaceSplitter_ = new QSplitter(Qt::Horizontal, central);
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(5);

    workspaceSplitter_->addWidget(createLibraryPanel(workspaceSplitter_));
    workspaceSplitter_->addWidget(createPreviewPanel(workspaceSplitter_));
    workspaceSplitter_->addWidget(createInspectorPanel(workspaceSplitter_));
    workspaceSplitter_->setStretchFactor(0, 0);
    workspaceSplitter_->setStretchFactor(1, 1);
    workspaceSplitter_->setStretchFactor(2, 0);
    workspaceSplitter_->setSizes({230, 700, 300});
    layout->addWidget(workspaceSplitter_, 1);
    layout->addWidget(createTransport(central), 0);
    setCentralWidget(central);
}

QWidget* MainWindow::createLibraryPanel(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    panel->setMinimumWidth(200);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(6);

    libraryHeader_ = sectionHeader(panel, QString{});
    playlistsHeader_ = sectionHeader(panel, QString{});
    layout->addWidget(libraryHeader_);
    layout->addWidget(playlistsHeader_);
    playlistList_ = new QListWidget(panel);
    playlistList_->setAlternatingRowColors(true);
    playlistList_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(playlistList_, 1);

    auto* playlistPlaybackModeRow = new QWidget(panel);
    playlistPlaybackModeRow->setObjectName(QStringLiteral("playlistPlaybackModeRow"));
    auto* playlistPlaybackModeLayout = new QHBoxLayout(playlistPlaybackModeRow);
    playlistPlaybackModeLayout->setContentsMargins(0, 0, 0, 0);
    playlistPlaybackModeLayout->setSpacing(6);
    playlistPlaybackModeLabel_ = new QLabel(playlistPlaybackModeRow);
    playlistPlaybackModeLabel_->setObjectName(QStringLiteral("playlistPlaybackModeLabel"));
    playlistPlaybackModeCombo_ = new QComboBox(playlistPlaybackModeRow);
    playlistPlaybackModeCombo_->setObjectName(QStringLiteral("playlistPlaybackModeCombo"));
    playlistPlaybackModeCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addPlaybackModeItems(*playlistPlaybackModeCombo_);
    playlistPlaybackModeLayout->addWidget(playlistPlaybackModeLabel_);
    playlistPlaybackModeLayout->addWidget(playlistPlaybackModeCombo_, 1);
    layout->addWidget(playlistPlaybackModeRow);

    addPlaylistButton_ = new QPushButton(panel);
    layout->addWidget(addPlaylistButton_);

    addSeparator(layout, panel);
    itemsHeader_ = sectionHeader(panel, QString{});
    layout->addWidget(itemsHeader_);
    itemList_ = new QListWidget(panel);
    itemList_->setAlternatingRowColors(true);
    itemList_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(itemList_, 1);

    auto* itemPlaybackModeRow = new QWidget(panel);
    itemPlaybackModeRow->setObjectName(QStringLiteral("itemPlaybackModeRow"));
    auto* itemPlaybackModeLayout = new QHBoxLayout(itemPlaybackModeRow);
    itemPlaybackModeLayout->setContentsMargins(0, 0, 0, 0);
    itemPlaybackModeLayout->setSpacing(6);
    itemPlaybackModeLabel_ = new QLabel(itemPlaybackModeRow);
    itemPlaybackModeLabel_->setObjectName(QStringLiteral("itemPlaybackModeLabel"));
    itemPlaybackModeCombo_ = new QComboBox(itemPlaybackModeRow);
    itemPlaybackModeCombo_->setObjectName(QStringLiteral("itemPlaybackModeCombo"));
    itemPlaybackModeCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addPlaybackModeItems(*itemPlaybackModeCombo_);
    itemPlaybackModeLayout->addWidget(itemPlaybackModeLabel_);
    itemPlaybackModeLayout->addWidget(itemPlaybackModeCombo_, 1);
    layout->addWidget(itemPlaybackModeRow);

    addMediaButton_ = new QPushButton(panel);
    layout->addWidget(addMediaButton_);

    QObject::connect(
        playlistPlaybackModeCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int index)
        {
            const auto mode = playlistPlaybackModeCombo_ == nullptr
                ? std::nullopt
                : playbackModeForComboIndex(*playlistPlaybackModeCombo_, index);
            if (mode.has_value())
            {
                (void)workspaceModel_.setPlaylistPlaybackMode(*mode);
            }
        });
    QObject::connect(
        itemPlaybackModeCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int index)
        {
            const auto mode = itemPlaybackModeCombo_ == nullptr
                ? std::nullopt
                : playbackModeForComboIndex(*itemPlaybackModeCombo_, index);
            const PlaylistId selectedPlaylistId = workspaceModel_.selectedPlaylistId();
            if (mode.has_value() && selectedPlaylistId.isValid())
            {
                (void)workspaceModel_.setPlaylistItemPlaybackMode(
                    selectedPlaylistId,
                    *mode);
            }
        });
    QObject::connect(
        playlistList_,
        &QListWidget::currentItemChanged,
        this,
        [this](QListWidgetItem* item)
        {
            const PlaylistId id = playlistIdFor(item);
            if (id.isValid() && workspaceModel_.selectPlaylist(id))
            {
                refreshItemList();
                refreshInspector();
                synchronizeActivePlaybackVolumeCluster();
            }
        });
    QObject::connect(
        itemList_,
        &QListWidget::currentItemChanged,
        this,
        [this](QListWidgetItem* item)
        {
            const ItemId id = itemIdFor(item);
            if (id.isValid())
            {
                (void)workspaceModel_.selectItem(id);
            }
            refreshInspector();
        });
    QObject::connect(
        playlistList_,
        &QListWidget::itemDoubleClicked,
        this,
        [this](QListWidgetItem* item)
        {
            activatePlaylist(playlistIdFor(item));
        });
    QObject::connect(
        itemList_,
        &QListWidget::itemDoubleClicked,
        this,
        [this](QListWidgetItem* item)
        {
            activateItem(itemIdFor(item));
        });
    QObject::connect(
        playlistList_,
        &QListWidget::customContextMenuRequested,
        this,
        [this](const QPoint& position)
        {
            showPlaylistContextMenu(position);
        });
    QObject::connect(
        itemList_,
        &QListWidget::customContextMenuRequested,
        this,
        [this](const QPoint& position)
        {
            showItemContextMenu(position);
        });
    QObject::connect(
        addPlaylistButton_,
        &QPushButton::clicked,
        this,
        [this]
        {
            addPlaylist();
        });
    QObject::connect(
        addMediaButton_,
        &QPushButton::clicked,
        this,
        [this]
        {
            addMedia();
        });

    refreshLibrary();
    return panel;
}

QWidget* MainWindow::createPreviewPanel(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* previewHeader = new QWidget(panel);
    auto* previewHeaderLayout = new QVBoxLayout(previewHeader);
    previewHeaderLayout->setContentsMargins(0, 0, 0, 0);
    previewHeaderLayout->setSpacing(3);

    auto* previewTitleRow = new QHBoxLayout();
    previewTitleRow->setContentsMargins(0, 0, 0, 0);
    previewTitleRow->setSpacing(6);
    previewHeader_ = sectionHeader(previewHeader, QString{});
    currentCanvasAspectLabel_ = new QLabel(previewHeader);
    currentCanvasAspectLabel_->setObjectName(QStringLiteral("currentCanvasAspectLabel"));
    currentCanvasAspectLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    previewTitleRow->addWidget(previewHeader_);
    previewTitleRow->addStretch(1);
    previewTitleRow->addWidget(currentCanvasAspectLabel_);
    previewHeaderLayout->addLayout(previewTitleRow);

    auto* screenRow = new QHBoxLayout();
    screenRow->setContentsMargins(0, 0, 0, 0);
    screenRow->setSpacing(4);
    mapToLabel_ = new QLabel(previewHeader);
    boundDisplayValueLabel_ = new QLabel(previewHeader);
    boundDisplayValueLabel_->setObjectName(QStringLiteral("boundDisplayValueLabel"));
    boundDisplayValueLabel_->setMinimumWidth(0);
    boundDisplayValueLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    identifyDisplayButton_ = new QToolButton(previewHeader);
    deployDisplayButton_ = new QToolButton(previewHeader);
    deployDisplayButton_->setObjectName(QStringLiteral("deployDisplayButton"));
    deployDisplayButton_->setEnabled(false);
    screenRow->addWidget(mapToLabel_);
    screenRow->addWidget(boundDisplayValueLabel_, 1);
    screenRow->addWidget(identifyDisplayButton_);
    screenRow->addWidget(deployDisplayButton_);
    previewHeaderLayout->addLayout(screenRow);
    layout->addWidget(previewHeader);

    previewViewportHost_ = new PreviewViewportHost(panel);
    canvasFrame_ = new DesktopSceneEngine::Player::CanvasChromeFrame(previewViewportHost_);
    canvasFrame_->setObjectName(QStringLiteral("previewCanvasFrame"));
    renderWidget_ = new RenderWidget(canvasFrame_);
    renderWidget_->setMinimumSize(1, 1);
    canvasFrame_->setCanvasSurface(renderWidget_);
    previewViewportHost_->setCanvasFrame(canvasFrame_);
    updateCanvasAspectUi();
    layout->addWidget(previewViewportHost_, 1);

    QObject::connect(
        identifyDisplayButton_,
        &QToolButton::clicked,
        this,
        [this]
        {
            identifyBoundDisplay();
        });
    QObject::connect(deployDisplayButton_, &QToolButton::clicked, this, [this]
    {
        toggleDesktopDeployment();
    });

    refreshDisplaySelection();
    return panel;
}

void MainWindow::refreshDisplaySelection()
{
    applyDisplayEnumerationResult(displayEnumeratorForTesting_
        ? displayEnumeratorForTesting_() : DesktopSceneEngine::Platform::enumerateActiveDisplays());
}

void MainWindow::resetWorkspaceForNewDocument()
{
    refreshDisplaySelection();
    DesktopSceneEngine::Domain::AuthoredProject project{
        requireDisplayOwner(displaySelection_.boundDisplayId())};
    if (const auto display = displaySelection_.boundDisplay())
    {
        if (const auto aspect = DesktopSceneEngine::Domain::AspectRatio::from(
                display->pixelWidth, display->pixelHeight))
            project.desktopCanvas.aspectRatio = *aspect;
    }
    // Display geometry seeds only a new document. Loaded/authored Canvas remains
    // authoritative, and unresolved identity keeps the existing safe default.
    workspaceModel_.replaceAuthoredProject(std::move(project));
}

void MainWindow::applyDisplayEnumerationResult(
    const DesktopSceneEngine::Platform::DisplayEnumerationResult& result)
{
    displaySelection_.refresh(result);
    updateBoundDisplayUi();
}

void MainWindow::updateCanvasAspectUi()
{
    const DesktopSceneEngine::Domain::AspectRatio& aspect =
        workspaceModel_.desktopCanvas().aspectRatio;
    if (currentCanvasAspectLabel_ != nullptr)
    {
        const QString ratio = QStringLiteral("%1:%2")
            .arg(aspect.numerator)
            .arg(aspect.denominator);
        currentCanvasAspectLabel_->setText(tr("Current Canvas Aspect — %1").arg(ratio));
    }

    if (previewViewportHost_ != nullptr)
    {
        previewViewportHost_->setCanvasAspectRatio(aspect);
    }
}

void MainWindow::updateBoundDisplayUi()
{
    if (boundDisplayValueLabel_ == nullptr)
    {
        return;
    }

    const auto descriptor = displaySelection_.boundDisplay();
    const auto availability = displaySelection_.availability();
    QString text{};
    if (availability == DesktopSceneEngine::Player::DisplaySelectionAvailability::EnumerationFailed)
    {
        text = tr("Unable to enumerate displays");
    }
    else if (availability == DesktopSceneEngine::Player::DisplaySelectionAvailability::Unresolved)
    {
        text = tr("Selected display unavailable");
    }
    else if (descriptor.has_value())
    {
        text = displayDescriptorLabel(*descriptor);
    }

    boundDisplayValueLabel_->setText(text);
    boundDisplayValueLabel_->setToolTip(text);

    if (identifyDisplayButton_ != nullptr)
    {
        identifyDisplayButton_->setEnabled(displaySelection_.identifyRequest().has_value());
    }
    updateDesktopDeploymentButton();
}

bool MainWindow::hasDesktopPresentation() const noexcept
{
    // Retained ownership counts even if the HWND has become invalid or cleanup
    // failed. The button must still allow safe removal of that endpoint.
    return desktopPresentationTargetId_.isValid() || desktopHost_.nativeHandle() != nullptr;
}

void MainWindow::updateDesktopDeploymentButton()
{
    if (deployDisplayButton_ == nullptr) return;
    const auto projection = desktopDeploymentProjection();
    deployDisplayButton_->setText(projection.retained ? tr("Undeploy") : tr("Deploy"));
    deployDisplayButton_->setEnabled(projection.retained ? projection.canUndeploy : projection.canDeploy);
}

void MainWindow::toggleDesktopDeployment()
{
    using DesktopSceneEngine::Player::DesktopDeploymentIntent;
    (void)requestDesktopDeployment(desktopDeploymentProjection().retained
        ? DesktopDeploymentIntent::Undeploy : DesktopDeploymentIntent::Deploy);
}

DesktopSceneEngine::Player::DesktopDeploymentProjection MainWindow::desktopDeploymentProjection() const
{
    const bool retained = desktopDeploymentDesired_ || hasDesktopPresentation();
    const bool ready = engineInitialized_ && !desktopReplacementInProgress_
        && !trayPresentation_.closePending() && !trayPresentation_.ending();
    return {retained, ready && (retained || displaySelection_.boundDisplay().has_value()), ready};
}

DesktopSceneEngine::Player::DesktopDeploymentResult MainWindow::requestDesktopDeployment(
    DesktopSceneEngine::Player::DesktopDeploymentIntent intent)
{
    using DesktopSceneEngine::Player::DesktopDeploymentIntent;
    using DesktopSceneEngine::Player::DesktopDeploymentResult;
    const auto projection = desktopDeploymentProjection();
    if (!engineInitialized_ || desktopReplacementInProgress_ || trayPresentation_.closePending()
        || trayPresentation_.ending()) return DesktopDeploymentResult::Rejected;
    if ((intent == DesktopDeploymentIntent::Deploy) == projection.retained)
        return DesktopDeploymentResult::AlreadySatisfied;
    bool completed = false;
    if (intent == DesktopDeploymentIntent::Undeploy)
    {
        // Cancel intent BEFORE detach/destroy, including any queued or recursive
        // native notification. Failed detach retains the borrowed HWND and ID.
        desktopDeploymentDesired_ = false;
        cancelDesktopPresentationReconciliation();
        {
            QScopedValueRollback<bool> removing(desktopReplacementInProgress_, true);
            completed = removeDesktopPresentation();
        }
    }
    else
    {
        refreshDisplaySelection(); // Target-local fresh exact-bound capability, not the tray's cached projection.
        if (!displaySelection_.boundDisplay().has_value()) return DesktopDeploymentResult::Rejected;
        desktopDeploymentDesired_ = true;
        completed = reconcileDesktopPresentation(false);
        if (!completed) desktopDeploymentDesired_ = false;
    }
    updateDesktopDeploymentButton();
    return completed ? DesktopDeploymentResult::Completed : DesktopDeploymentResult::Failure;
}

void MainWindow::identifyBoundDisplay()
{
    const auto request = displaySelection_.identifyRequest();
    if (!request.has_value())
    {
        return;
    }

    const DesktopSceneEngine::Platform::DisplayIdentifyStatus status
        = DesktopSceneEngine::Platform::identifyDisplay(
            request->displayId,
            IdentifyDurationMilliseconds);
    if (status != DesktopSceneEngine::Platform::DisplayIdentifyStatus::Shown)
    {
        refreshDisplaySelection();
    }
}

QString MainWindow::displayDescriptorLabel(
    const DesktopSceneEngine::Platform::DisplayDescriptor& descriptor
) const
{
    QString label = tr("Display %1").arg(descriptor.displayNumber);
    if (!descriptor.friendlyName.empty())
    {
        label += QStringLiteral(" — ") + QString::fromStdString(descriptor.friendlyName);
    }
    label += QStringLiteral(" — %1×%2")
        .arg(descriptor.pixelWidth)
        .arg(descriptor.pixelHeight);
    if (descriptor.primary)
    {
        label += QStringLiteral(" (") + tr("Primary") + QStringLiteral(")");
    }
    return label;
}

QWidget* MainWindow::createInspectorPanel(QWidget* parent)
{
    inspectorScrollArea_ = new QScrollArea(parent);
    inspectorScrollArea_->setMinimumWidth(240);
    inspectorScrollArea_->setWidgetResizable(true);
    inspectorScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspectorScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    refreshInspector();
    return inspectorScrollArea_;
}

QWidget* MainWindow::createTransport(QWidget* parent)
{
    auto* controls = new QWidget(parent);
    auto* layout = new QHBoxLayout(controls);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    previousButton_ = new QPushButton(controls);
    playPauseButton_ = new QPushButton(controls);
    nextButton_ = new QPushButton(controls);
    volumeCluster_ = new QWidget(controls);
    volumeCluster_->setObjectName(QStringLiteral("volumeCluster"));
    auto* volumeClusterLayout = new QVBoxLayout(volumeCluster_);
    volumeClusterLayout->setContentsMargins(0, 0, 0, 0);
    volumeClusterLayout->setSpacing(2);
    auto* volumeControlsLayout = new QHBoxLayout();
    volumeControlsLayout->setContentsMargins(0, 0, 0, 0);
    volumeControlsLayout->setSpacing(6);
    volumeLabel_ = new QLabel(volumeCluster_);
    muteButton_ = new QPushButton(volumeCluster_);
    volumeSlider_ = new QSlider(Qt::Horizontal, volumeCluster_);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(100);
    volumeSlider_->setFixedWidth(140);
    volumeControlsLayout->addWidget(volumeLabel_);
    volumeControlsLayout->addWidget(volumeSlider_);
    volumeControlsLayout->addWidget(muteButton_);
    volumeClusterLayout->addLayout(volumeControlsLayout);
    activePlaylistNameLabel_ = new QLabel(volumeCluster_);
    activePlaylistNameLabel_->setObjectName(QStringLiteral("activePlaylistNameLabel"));
    activePlaylistNameLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    activePlaylistNameLabel_->setWordWrap(false);
    activePlaylistNameLabel_->setMinimumWidth(0);
    activePlaylistNameLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    activePlaylistNameLabel_->installEventFilter(this);
    volumeClusterLayout->addWidget(activePlaylistNameLabel_);
    volumeCluster_->setVisible(false);
    positionSlider_ = new QSlider(Qt::Horizontal, controls);
    positionSlider_->setRange(
        0,
        DesktopSceneEngine::Player::TimelineSliderMaximum);
    positionSlider_->setEnabled(false);
    transportTimeLabel_ = new QLabel(QStringLiteral("--:-- / --:--"), controls);
    transportTimeLabel_->setMinimumWidth(140);
    transportTimeLabel_->setAlignment(Qt::AlignCenter);

    layout->addWidget(previousButton_);
    layout->addWidget(playPauseButton_);
    layout->addWidget(nextButton_);
    layout->addWidget(positionSlider_, 1);
    layout->addWidget(transportTimeLabel_);
    layout->addWidget(volumeCluster_);

    QObject::connect(
        previousButton_,
        &QPushButton::clicked,
        this,
        [this]
        {
            navigateScene(true);
        });
    QObject::connect(
        nextButton_,
        &QPushButton::clicked,
        this,
        [this]
        {
            navigateScene(false);
        });
    QObject::connect(
        positionSlider_,
        &QSlider::sliderPressed,
        this,
        [this]
        {
            beginTimelineDrag();
        });
    QObject::connect(
        positionSlider_,
        &QSlider::sliderMoved,
        this,
        [this](int value)
        {
            previewTimelinePosition(value);
        });
    QObject::connect(
        positionSlider_,
        &QSlider::sliderReleased,
        this,
        [this]
        {
            commitTimelineDrag();
        });
    return controls;
}

void MainWindow::createTimelineHoverPopup()
{
    QWidget* const central = centralWidget();
    if (central == nullptr || positionSlider_ == nullptr)
    {
        return;
    }

    timelineHoverPopup_ = new TimelineHoverPopup(central);
    timelineHoverPopup_->setUnavailableText(tr("Preview unavailable"));
    timelineHoverPopup_->setPendingText(tr("Loading preview…"));
    refreshTimelineHoverPreviewPresentation();
    timelineHoverPopup_->hide();

    positionSlider_->setMouseTracking(true);
    positionSlider_->installEventFilter(this);
    if (previewViewportHost_ != nullptr)
    {
        previewViewportHost_->installEventFilter(this);
    }
    if (workspaceSplitter_ != nullptr)
    {
        QObject::connect(
            workspaceSplitter_,
            &QSplitter::splitterMoved,
            this,
            [this]
            {
                refreshTimelineHoverPreviewPresentation();
                updateTimelineHoverPopupGeometry();
            });
    }

    timelineHoverPreviewTimer_.setSingleShot(true);
    timelineHoverPreviewTimer_.setInterval(75);
    QObject::connect(
        &timelineHoverPreviewTimer_,
        &QTimer::timeout,
        this,
        [this]
        {
            decodeTimelineHoverPreview();
        });

    startTimelinePreviewWorker();
}

void MainWindow::startTimelinePreviewWorker()
{
    if (timelinePreviewWorker_ != nullptr)
    {
        return;
    }

    qRegisterMetaType<DesktopSceneEngine::Player::TimelinePreviewResult>();
    auto* worker = new DesktopSceneEngine::Player::TimelinePreviewWorker{
        timelinePreviewRequestState_,
        timelinePreviewMediaCacheRoot()};
    worker->moveToThread(&timelinePreviewWorkerThread_);
    QObject::connect(
        worker,
        &DesktopSceneEngine::Player::TimelinePreviewWorker::previewReady,
        this,
        [this](const DesktopSceneEngine::Player::TimelinePreviewResult& result)
        {
            applyTimelineHoverPreviewResult(result);
        },
        Qt::QueuedConnection);
    QObject::connect(
        &timelinePreviewWorkerThread_,
        &QThread::finished,
        worker,
        &QObject::deleteLater);

    timelinePreviewWorker_ = worker;
    timelinePreviewWorkerThread_.start();
}

void MainWindow::shutdownTimelinePreviewWorker() noexcept
{
    timelinePreviewRequestState_.stop();
    if (timelinePreviewWorker_ != nullptr && timelinePreviewWorkerThread_.isRunning())
    {
        const bool queued = QMetaObject::invokeMethod(
            timelinePreviewWorker_,
            &DesktopSceneEngine::Player::TimelinePreviewWorker::stop,
            Qt::QueuedConnection);
        if (!queued)
        {
            timelinePreviewWorkerThread_.quit();
        }
        timelinePreviewWorkerThread_.wait();
    }

    timelinePreviewWorker_ = nullptr;
}

void MainWindow::refreshLibrary()
{
    refreshPlaylistList();
    refreshItemList();
    refreshInspector();
}

void MainWindow::queueInspectorRefresh()
{
    if (inspectorRefreshPending_)
    {
        return;
    }

    inspectorRefreshPending_ = true;
    QTimer::singleShot(0, this, [this]
    {
        inspectorRefreshPending_ = false;
        refreshInspector();
    });
}

void MainWindow::refreshPlaylistList()
{
    if (playlistList_ == nullptr)
    {
        return;
    }

    const QSignalBlocker blocker{playlistList_};
    playlistList_->clear();

    const PlaylistId selectedId = workspaceModel_.selectedPlaylistId();
    const PlaylistId activeId = workspaceModel_.activePlaylistId();
    for (const auto& playlist : workspaceModel_.playlists())
    {
        auto* listItem = new QListWidgetItem(
            displayAlias(playlist.displayAlias, playlist.id == activeId),
            playlistList_);
        listItem->setData(workspaceIdRole, playlist.id.value);
        listItem->setToolTip(playlist.displayAlias);
        if (!playlist.enabled)
        {
            listItem->setForeground(
                palette().brush(QPalette::Disabled, QPalette::Text));
        }
        if (playlist.id == selectedId)
        {
            playlistList_->setCurrentItem(listItem);
        }
    }

    refreshPlaylistPlaybackModeControl();
    synchronizeActivePlaybackVolumeCluster();
}

void MainWindow::refreshItemList()
{
    refreshItemPlaybackModeControl();

    if (itemList_ == nullptr)
    {
        return;
    }

    const QSignalBlocker blocker{itemList_};
    itemList_->clear();

    const auto* selectedPlaylist = workspaceModel_.playlist(
        workspaceModel_.selectedPlaylistId());
    if (selectedPlaylist == nullptr)
    {
        return;
    }

    const auto& selectedItemId = workspaceModel_.selectedItemId();
    const auto& activeItemId = workspaceModel_.activeItemId();
    for (const auto& item : selectedPlaylist->items)
    {
        const bool active = activeItemId.has_value()
            && item.id == *activeItemId;
        auto* listItem = new QListWidgetItem(
            displayAlias(item.displayAlias, active),
            itemList_);
        listItem->setData(workspaceIdRole, item.id.value);
        listItem->setToolTip(item.displayAlias);
        if (!workspaceModel_.isItemPlayable(selectedPlaylist->id, item.id))
        {
            listItem->setForeground(
                palette().brush(QPalette::Disabled, QPalette::Text));
        }
        if (selectedItemId.has_value() && item.id == *selectedItemId)
        {
            itemList_->setCurrentItem(listItem);
        }
    }
}

void MainWindow::refreshPlaylistPlaybackModeControl()
{
    if (playlistPlaybackModeCombo_ == nullptr)
    {
        return;
    }

    const int index = playlistPlaybackModeCombo_->findData(
        static_cast<int>(workspaceModel_.playlistPlaybackMode()));
    if (index < 0)
    {
        return;
    }

    const QSignalBlocker blocker{playlistPlaybackModeCombo_};
    playlistPlaybackModeCombo_->setCurrentIndex(index);
}

void MainWindow::refreshItemPlaybackModeControl()
{
    if (itemPlaybackModeCombo_ == nullptr)
    {
        return;
    }

    const auto mode = workspaceModel_.playlistItemPlaybackMode(
        workspaceModel_.selectedPlaylistId());
    const QSignalBlocker blocker{itemPlaybackModeCombo_};
    itemPlaybackModeCombo_->setEnabled(mode.has_value());
    if (!mode.has_value())
    {
        itemPlaybackModeCombo_->setCurrentIndex(-1);
        return;
    }

    const int index = itemPlaybackModeCombo_->findData(static_cast<int>(*mode));
    if (index >= 0)
    {
        itemPlaybackModeCombo_->setCurrentIndex(index);
    }
}

void MainWindow::refreshInspector()
{
    if (inspectorScrollArea_ == nullptr)
    {
        return;
    }

    QWidget* previousContent = inspectorScrollArea_->takeWidget();
    inspectorContent_ = nullptr;
    inspectorLayout_ = nullptr;
    currentPlaybackRateValueLabel_ = nullptr;
    inspectorPlaylistMasterVolumeSlider_ = nullptr;
    inspectorPlaylistMasterVolumeValueLabel_ = nullptr;
    inspectorPlaylistMasterVolumePlaylistId_ = {};
    delete previousContent;

    inspectorContent_ = new QWidget(inspectorScrollArea_);
    makeHorizontallyShrinkable(inspectorContent_);
    inspectorLayout_ = new QVBoxLayout(inspectorContent_);
    inspectorLayout_->setContentsMargins(10, 10, 10, 10);
    inspectorLayout_->setSpacing(8);
    inspectorScrollArea_->setWidget(inspectorContent_);

    const auto& selectedItemId = workspaceModel_.selectedItemId();
    if (selectedItemId.has_value() && workspaceModel_.item(*selectedItemId) != nullptr)
    {
        buildItemInspector(*selectedItemId);
        return;
    }

    const PlaylistId selectedPlaylistId = workspaceModel_.selectedPlaylistId();
    if (workspaceModel_.playlist(selectedPlaylistId) != nullptr)
    {
        buildPlaylistInspector(selectedPlaylistId);
        return;
    }

    buildNoSelectionInspector();
}

void MainWindow::buildNoSelectionInspector()
{
    auto* message = new QLabel(tr("No selection"), inspectorContent_);
    message->setWordWrap(true);
    message->setStyleSheet(QStringLiteral("color: #8c98a5;"));
    inspectorLayout_->addWidget(message);
    inspectorLayout_->addStretch(1);
}

void MainWindow::buildPlaylistInspector(PlaylistId id)
{
    const auto* playlist = workspaceModel_.playlist(id);
    if (playlist == nullptr)
    {
        buildNoSelectionInspector();
        return;
    }

    inspectorLayout_->addWidget(sectionHeader(inspectorContent_, tr("PLAYLIST")));
    auto* form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* alias = new QLineEdit(playlist->displayAlias, inspectorContent_);
    makeHorizontallyShrinkable(alias);
    form->addRow(tr("Name"), alias);
    QObject::connect(alias, &QLineEdit::editingFinished, this, [this, id, alias]
    {
        const auto* current = workspaceModel_.playlist(id);
        if (current == nullptr)
        {
            refreshInspector();
            return;
        }

        if (!workspaceModel_.renamePlaylist(id, alias->text()))
        {
            const QSignalBlocker blocker{alias};
            alias->setText(current->displayAlias);
            return;
        }

        refreshOpenBackgroundMusicWindow(id);
        refreshPlaylistList();
        refreshStatus();
    });

    auto* itemCount = new QLabel(
        QString::number(playlist->items.size()),
        inspectorContent_);
    form->addRow(tr("Item Count"), itemCount);

    auto* masterVolumeRow = new QWidget(inspectorContent_);
    auto* masterVolumeLayout = new QHBoxLayout(masterVolumeRow);
    masterVolumeLayout->setContentsMargins(0, 0, 0, 0);
    auto* masterVolume = new QSlider(Qt::Horizontal, masterVolumeRow);
    masterVolume->setRange(0, 100);
    masterVolume->setValue(static_cast<int>(std::lround(playlist->masterVolume * 100.0)));
    masterVolume->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* masterVolumeValue = new QLabel(masterVolumeRow);
    masterVolumeValue->setMinimumWidth(38);
    masterVolumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    masterVolumeValue->setText(QStringLiteral("%1%").arg(masterVolume->value()));
    masterVolumeLayout->addWidget(masterVolume, 1);
    masterVolumeLayout->addWidget(masterVolumeValue);
    form->addRow(tr("Master Volume"), masterVolumeRow);
    inspectorPlaylistMasterVolumeSlider_ = masterVolume;
    inspectorPlaylistMasterVolumeValueLabel_ = masterVolumeValue;
    inspectorPlaylistMasterVolumePlaylistId_ = id;
    QObject::connect(
        masterVolume,
        &QSlider::valueChanged,
        this,
        [this, id](int value)
        {
            if (!workspaceModel_.setPlaylistMasterVolume(
                    id,
                    static_cast<double>(value) / 100.0))
            {
                return;
            }

            synchronizePlaylistInspectorMasterVolume(id);
            synchronizeActivePlaybackVolumeCluster();
            if (activePlaybackProjection_.has_value()
                && activePlaybackProjection_->sourcePlaylistId == id)
            {
                updateEffectiveOutputVolume();
            }
            refreshStatus();
        });

    inspectorLayout_->addLayout(form);
    inspectorLayout_->addStretch(1);
}

void MainWindow::buildItemInspector(ItemId id)
{
    const auto* item = workspaceModel_.item(id);
    if (item == nullptr)
    {
        buildNoSelectionInspector();
        return;
    }

    const PlaylistId playlistId = workspaceModel_.selectedPlaylistId();
    const bool isImage = item->mediaType == WorkspaceMediaType::Image;
    inspectorLayout_->addWidget(sectionHeader(inspectorContent_, tr("ITEM")));
    auto* itemInformationForm = new QFormLayout();
    itemInformationForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    itemInformationForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    itemInformationForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* alias = new QLineEdit(item->displayAlias, inspectorContent_);
    makeHorizontallyShrinkable(alias);
    itemInformationForm->addRow(tr("Name"), alias);
    QObject::connect(alias, &QLineEdit::editingFinished, this, [this, id, playlistId, alias]
    {
        const auto* current = workspaceModel_.item(id);
        if (current == nullptr)
        {
            refreshInspector();
            return;
        }

        if (!workspaceModel_.renameItem(id, alias->text()))
        {
            const QSignalBlocker blocker{alias};
            alias->setText(current->displayAlias);
            return;
        }

        refreshOpenBackgroundMusicWindow(playlistId);
        refreshItemList();
    });

    auto* type = new QLabel(isImage ? tr("Image") : tr("Video"), inspectorContent_);
    itemInformationForm->addRow(tr("Type"), type);

    auto* source = new QLabel(item->source, inspectorContent_);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    source->setToolTip(item->source);
    makeHorizontallyShrinkable(source);
    itemInformationForm->addRow(tr("Source"), source);
    inspectorLayout_->addLayout(itemInformationForm);

    if (isImage)
    {
        auto* imagePropertiesForm = new QFormLayout();
        imagePropertiesForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        imagePropertiesForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        imagePropertiesForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto* duration = new QDoubleSpinBox(inspectorContent_);
        duration->setRange(0.1, 86400.0);
        duration->setDecimals(2);
        duration->setSingleStep(0.1);
        duration->setSuffix(tr(" s"));
        duration->setValue(item->displayDurationSeconds);
        imagePropertiesForm->addRow(tr("Display Duration"), duration);
        QObject::connect(
            duration,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, id](double value)
            {
                (void)workspaceModel_.setImageDisplayDuration(id, value);
            });
        inspectorLayout_->addLayout(imagePropertiesForm);
    }
    else
    {
        auto* playbackRateForm = new QFormLayout();
        playbackRateForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        playbackRateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        playbackRateForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto* currentPlaybackRate = new QLabel(
            QString::number(engine_.playbackSpeed(), 'f', 2),
            inspectorContent_);
        currentPlaybackRateValueLabel_ = currentPlaybackRate;
        playbackRateForm->addRow(
            tr("Current Playback Rate (0.5–2.0)"),
            currentPlaybackRate);
        inspectorLayout_->addLayout(playbackRateForm);

        auto* playbackRateRow = new QWidget(inspectorContent_);
        makeHorizontallyShrinkable(playbackRateRow);
        auto* playbackRateLayout = new QHBoxLayout(playbackRateRow);
        playbackRateLayout->setContentsMargins(0, 0, 0, 0);
        playbackRateLayout->setSpacing(0);
        auto* playbackRateLabel = new QLabel(tr("Playback Rate"), playbackRateRow);
        playbackRateLabel->setWordWrap(true);
        playbackRateLabel->setMinimumWidth(0);
        playbackRateLabel->setSizePolicy(
            QSizePolicy::Preferred,
            QSizePolicy::Preferred);
        auto* playbackRate = new QDoubleSpinBox(playbackRateRow);
        playbackRate->setRange(0.5, 2.0);
        playbackRate->setDecimals(1);
        playbackRate->setSingleStep(0.1);
        playbackRate->setKeyboardTracking(false);
        playbackRate->setValue(item->playbackRate);
        playbackRate->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        playbackRate->setFixedWidth(playbackRate->sizeHint().width());
        auto* applyPlaybackRate = new QPushButton(tr("Apply"), playbackRateRow);
        applyPlaybackRate->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const int controlGap = playbackRateRow->fontMetrics().horizontalAdvance(
            QStringLiteral("  "));
        playbackRateLayout->addWidget(playbackRateLabel);
        playbackRateLayout->addStretch(1);
        playbackRateLayout->addWidget(playbackRate);
        playbackRateLayout->addSpacing(controlGap);
        playbackRateLayout->addWidget(applyPlaybackRate);
        QObject::connect(
            applyPlaybackRate,
            &QPushButton::clicked,
            this,
            [this, id, playbackRate]
            {
                const double configuredRate = playbackRate->value();
                if (!workspaceModel_.setVideoPlaybackRate(id, configuredRate))
                {
                    const auto* current = workspaceModel_.item(id);
                    if (current != nullptr)
                    {
                        const QSignalBlocker blocker{playbackRate};
                        playbackRate->setValue(current->playbackRate);
                    }
                    return;
                }

                const auto& activeItemId = workspaceModel_.activeItemId();
                if (activePlaybackProjection_.has_value() && activeItemId.has_value()
                    && *activeItemId == id
                    && activePlaybackProjection_->sceneIndexForItem(id).has_value())
                {
                    engine_.setPlaybackSpeed(static_cast<float>(configuredRate));
                }

                refreshStatus();
            });
        inspectorLayout_->addWidget(playbackRateRow);
    }

    if (!isImage)
    {
        addVideoItemAudioInspectorSection(id);
    }

    addVisualInspectorSection(id);
    inspectorLayout_->addStretch(1);
}

void MainWindow::addVideoItemAudioInspectorSection(ItemId itemId)
{
    const auto* item = workspaceModel_.item(itemId);
    if (item == nullptr || item->mediaType != WorkspaceMediaType::Video)
    {
        return;
    }

    addSeparator(inspectorLayout_, inspectorContent_);
    inspectorLayout_->addWidget(sectionHeader(inspectorContent_, tr("Audio")));

    auto* volumeRow = new QWidget(inspectorContent_);
    makeHorizontallyShrinkable(volumeRow);
    auto* volumeLayout = new QHBoxLayout(volumeRow);
    volumeLayout->setContentsMargins(0, 0, 0, 0);
    auto* volumeLabel = new QLabel(tr("Volume"), volumeRow);
    auto* volume = new QSlider(Qt::Horizontal, volumeRow);
    volume->setRange(0, 100);
    volume->setValue(static_cast<int>(std::lround(item->itemMediaVolume * 100.0)));
    auto* volumeValue = new QLabel(QStringLiteral("%1%").arg(volume->value()), volumeRow);
    volumeValue->setMinimumWidth(38);
    volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volumeLayout->addWidget(volumeLabel);
    volumeLayout->addWidget(volume, 1);
    volumeLayout->addWidget(volumeValue);
    inspectorLayout_->addWidget(volumeRow);

    QObject::connect(
        volume,
        &QSlider::valueChanged,
        this,
        [this, itemId, volumeValue](int value)
        {
            if (workspaceModel_.setItemMediaVolume(
                    itemId,
                    static_cast<double>(value) / 100.0))
            {
                volumeValue->setText(QStringLiteral("%1%").arg(value));
                if (activeTimelineItemId() == itemId)
                {
                    updateEffectiveOutputVolume();
                }
            }
        });
}

void MainWindow::addVisualInspectorSection(ItemId itemId)
{
    const auto* item = workspaceModel_.item(itemId);
    if (item == nullptr)
    {
        return;
    }

    addSeparator(inspectorLayout_, inspectorContent_);
    inspectorLayout_->addWidget(sectionHeader(inspectorContent_, tr("VISUAL")));
    auto* form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    const auto commitPresentation = [this, itemId](
        const DesktopSceneEngine::Player::WorkspaceItemPresentation& presentation)
    {
        if (!workspaceModel_.setItemPresentation(itemId, presentation))
        {
            return;
        }

        const auto& activeItemId = workspaceModel_.activeItemId();
        if (activePlaybackProjection_.has_value() && activeItemId.has_value()
            && *activeItemId == itemId)
        {
            (void)rebuildActiveProjection(
                itemId,
                ActiveProjectionRebuildMode::RestoreCurrentItem);
            refreshTimelineHoverPreviewPresentation(true);
        }
    };

    auto* fitAspect = new QComboBox(inspectorContent_);
    fitAspect->setObjectName(QStringLiteral("visualFitAspectCombo"));
    fitAspect->addItem(tr("Native"),
        static_cast<int>(DesktopSceneEngine::Domain::MediaPresentationMode::Native));
    fitAspect->addItem(tr("Auto Fill"),
        static_cast<int>(DesktopSceneEngine::Domain::MediaPresentationMode::AutoFill));
    fitAspect->addItem(tr("Custom Aspect"),
        static_cast<int>(DesktopSceneEngine::Domain::MediaPresentationMode::Custom));
    fitAspect->setCurrentIndex(static_cast<int>(item->presentation.mediaPresentation.mode));
    form->addRow(tr("Fit / Aspect"), fitAspect);

    auto* aspectRow = new QWidget(inspectorContent_);
    aspectRow->setObjectName(QStringLiteral("visualAspectRatioRow"));
    makeHorizontallyShrinkable(aspectRow);
    auto* aspectLayout = new QHBoxLayout(aspectRow);
    aspectLayout->setContentsMargins(0, 0, 0, 0);
    auto* preset = new QComboBox(aspectRow);
    preset->setObjectName(QStringLiteral("visualAspectPresetCombo"));
    preset->addItem(QStringLiteral("16:9"));
    preset->addItem(QStringLiteral("16:10"));
    preset->addItem(QStringLiteral("4:3"));
    preset->addItem(QStringLiteral("1:1"));
    preset->addItem(tr("Custom"));
    auto* numerator = new QSpinBox(aspectRow);
    numerator->setObjectName(QStringLiteral("visualAspectNumeratorSpinBox"));
    auto* denominator = new QSpinBox(aspectRow);
    denominator->setObjectName(QStringLiteral("visualAspectDenominatorSpinBox"));
    numerator->setRange(1, 10000);
    denominator->setRange(1, 10000);
    numerator->setValue(item->presentation.mediaPresentation.customAspect.numerator);
    denominator->setValue(item->presentation.mediaPresentation.customAspect.denominator);
    numerator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    denominator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    aspectLayout->addWidget(preset, 1);
    aspectLayout->addWidget(numerator);
    aspectLayout->addWidget(new QLabel(QStringLiteral(":"), aspectRow));
    aspectLayout->addWidget(denominator);
    form->addRow(tr("Aspect Ratio"), aspectRow);

    const auto selectedPreset = [numerator, denominator]()
    {
        const int width = numerator->value();
        const int height = denominator->value();
        if (width == 16 && height == 9)
        {
            return 0;
        }
        if (width == 16 && height == 10)
        {
            return 1;
        }
        if (width == 4 && height == 3)
        {
            return 2;
        }
        if (width == 1 && height == 1)
        {
            return 3;
        }
        return 4;
    };
    preset->setCurrentIndex(selectedPreset());
    const auto commitCustomAspect = [this, itemId, numerator, denominator,
                                     commitPresentation]()
    {
        const auto* current = workspaceModel_.item(itemId);
        if (current == nullptr)
        {
            return;
        }

        auto presentation = current->presentation;
        const auto aspect = DesktopSceneEngine::Domain::AspectRatio::from(
            numerator->value(), denominator->value());
        if (!aspect.has_value())
        {
            return;
        }
        presentation.mediaPresentation.customAspect = *aspect;
        commitPresentation(presentation);
    };
    QObject::connect(
        preset,
        QOverload<int>::of(&QComboBox::activated),
        this,
        [preset, numerator, denominator, commitCustomAspect](int index)
        {
            constexpr int customPresetIndex = 4;
            if (index == customPresetIndex)
            {
                return;
            }

            constexpr int ratios[][2] = {{16, 9}, {16, 10}, {4, 3}, {1, 1}};
            const QSignalBlocker numeratorBlocker{numerator};
            const QSignalBlocker denominatorBlocker{denominator};
            numerator->setValue(ratios[index][0]);
            denominator->setValue(ratios[index][1]);
            commitCustomAspect();
        });
    QObject::connect(
        numerator,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        commitCustomAspect);
    QObject::connect(
        denominator,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        commitCustomAspect);

    auto* scale = new QDoubleSpinBox(inspectorContent_);
    scale->setObjectName(QStringLiteral("visualScaleSpinBox"));
    scale->setRange(0.1, 3.0);
    scale->setDecimals(2);
    scale->setSingleStep(0.1);
    scale->setValue(item->presentation.layout.scaleX);
    form->addRow(tr("Scale"), scale);
    QObject::connect(
        scale,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        [this, itemId, commitPresentation](double value)
        {
            const auto* current = workspaceModel_.item(itemId);
            if (current == nullptr)
            {
                return;
            }

            auto presentation = current->presentation;
            presentation.layout.scaleX = static_cast<float>(value);
            presentation.layout.scaleY = static_cast<float>(value);
            commitPresentation(presentation);
        });

    auto* positionX = new QDoubleSpinBox(inspectorContent_);
    positionX->setObjectName(QStringLiteral("visualPositionXSpinBox"));
    auto* positionY = new QDoubleSpinBox(inspectorContent_);
    positionY->setObjectName(QStringLiteral("visualPositionYSpinBox"));
    for (QDoubleSpinBox* control : {positionX, positionY})
    {
        control->setRange(-1.0, 1.0);
        control->setDecimals(2);
        control->setSingleStep(0.05);
        control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    positionX->setValue(item->presentation.layout.positionX);
    positionY->setValue(item->presentation.layout.positionY);
    form->addRow(tr("Position X"), positionX);
    form->addRow(tr("Position Y"), positionY);
    const auto commitPosition = [this, itemId, positionX, positionY,
                                 commitPresentation](double)
    {
        const auto* current = workspaceModel_.item(itemId);
        if (current == nullptr)
        {
            return;
        }

        auto presentation = current->presentation;
        presentation.layout.positionX = static_cast<float>(positionX->value());
        presentation.layout.positionY = static_cast<float>(positionY->value());
        commitPresentation(presentation);
    };
    QObject::connect(
        positionX,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        commitPosition);
    QObject::connect(
        positionY,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        commitPosition);

    const auto updateVisualPolicyControls = [form, fitAspect, aspectRow]()
    {
        form->setRowVisible(
            aspectRow,
            fitAspect->currentData().toInt()
                == static_cast<int>(DesktopSceneEngine::Domain::MediaPresentationMode::Custom));
    };
    updateVisualPolicyControls();

    QObject::connect(
        fitAspect,
        QOverload<int>::of(&QComboBox::activated),
        this,
        [this, itemId, fitAspect, commitPresentation, updateVisualPolicyControls](int)
        {
            const auto* current = workspaceModel_.item(itemId);
            if (current == nullptr)
            {
                return;
            }

            auto presentation = current->presentation;
            presentation.mediaPresentation.mode =
                static_cast<DesktopSceneEngine::Domain::MediaPresentationMode>(
                    fitAspect->currentData().toInt());
            commitPresentation(presentation);
            updateVisualPolicyControls();
        });

    inspectorLayout_->addLayout(form);
}

void MainWindow::addPlaylist()
{
    const PlaylistId id = workspaceModel_.addPlaylist();
    if (workspaceModel_.selectPlaylist(id))
    {
        refreshLibrary();
    }
}

void MainWindow::addMedia()
{
    const QStringList sources = QFileDialog::getOpenFileNames(
        this,
        tr("Add Media"),
        {},
        tr("Media Files (*.png *.jpg *.jpeg *.webp *.mp4 *.avi *.mov *.mkv *.webm *.video)"));
    if (sources.isEmpty())
    {
        return;
    }

    const PlaylistId playlistId = workspaceModel_.selectedPlaylistId();
    const bool synchronizesActivePlaylist = activePlaybackProjection_.has_value()
        && playlistId == activePlaybackProjection_->sourcePlaylistId;
    const std::optional<ItemId> activeItemId = synchronizesActivePlaylist
        ? activePlaybackProjection_->itemForSceneIndex(engine_.activeSceneIndex())
        : std::nullopt;
    bool added = false;
    for (const QString& source : sources)
    {
        const auto mediaType = mediaTypeForSource(source);
        if (!mediaType.has_value())
        {
            QMessageBox::warning(
                this,
                tr("Unsupported Media"),
                tr("Unsupported media file: %1").arg(source));
            continue;
        }

        const QFileInfo fileInfo{source};
        if (workspaceModel_.addItem(
            playlistId,
            *mediaType,
            source,
            fileInfo.completeBaseName()).has_value())
        {
            added = true;
        }
    }

    if (added && synchronizesActivePlaylist)
    {
        const ActiveProjectionRebuildResult rebuildResult = activeItemId.has_value()
            ? rebuildActiveProjection(
                *activeItemId,
                ActiveProjectionRebuildMode::RestoreCurrentItem)
            : ActiveProjectionRebuildResult::Unchanged;
        if (rebuildResult == ActiveProjectionRebuildResult::ReboundAtStart)
        {
            QMessageBox::warning(
                this,
                tr("Playlist updated"),
                tr("Playlist updated, but the previous playback position could not be restored."));
        }
        else if (rebuildResult != ActiveProjectionRebuildResult::Restored)
        {
            QMessageBox::warning(
                this,
                tr("Unable to synchronize playback"),
                tr("Newly added media could not be synchronized with active playback. "
                   "It will be available the next time the playlist is played."));
        }
    }
    if (added)
    {
        refreshOpenBackgroundMusicWindow(playlistId);
    }
    refreshLibrary();
}

DesktopSceneEngine::Player::BackgroundMusicWindow* MainWindow::openBackgroundMusicWindow(
    PlaylistId id)
{
    if (!id.isValid() || workspaceModel_.playlist(id) == nullptr)
    {
        return nullptr;
    }

    auto iterator = backgroundMusicWindows_.find(id.value);
    if (iterator != backgroundMusicWindows_.end())
    {
        if (auto* window = iterator.value().data())
        {
            window->refreshFromModel();
            synchronizeBackgroundMusicPlaybackDisplay();
            applyBackgroundMusicPlaybackDisplayState(*window);
            window->show();
            window->raise();
            window->activateWindow();
            return window;
        }

        backgroundMusicWindows_.erase(iterator);
    }

    auto* window = new DesktopSceneEngine::Player::BackgroundMusicWindow(
        workspaceModel_,
        id,
        nullptr);
    const QPointer<MainWindow> owner{this};
    window->setQueueChangedCallback([owner](PlaylistId playlistId, std::optional<ItemId> itemId)
    {
        if (!owner.isNull())
        {
            owner->handleBackgroundMusicQueueChanged(playlistId, itemId);
        }
    });
    window->setPlaylistEnabledChangedCallback([owner](PlaylistId playlistId, bool enabled)
    {
        if (!owner.isNull())
        {
            owner->handlePlaylistMusicEnabledChanged(playlistId, enabled);
        }
    });
    window->setItemEnabledChangedCallback(
        [owner](PlaylistId playlistId, ItemId itemId, bool enabled)
        {
            if (!owner.isNull())
            {
                owner->handleItemMusicEnabledChanged(playlistId, itemId, enabled);
            }
        });
    window->setPlaylistTrackSwitchRequestedCallback([owner](PlaylistId playlistId, TrackId trackId)
    {
        if (!owner.isNull())
        {
            owner->handlePlaylistTrackSwitchRequested(playlistId, trackId);
        }
    });
    window->setItemTrackSwitchRequestedCallback(
        [owner](PlaylistId playlistId, ItemId itemId, TrackId trackId)
        {
            if (!owner.isNull())
            {
                owner->handleItemTrackSwitchRequested(playlistId, itemId, trackId);
            }
        });
    window->setAttribute(Qt::WA_DeleteOnClose);
    backgroundMusicWindows_.insert(id.value, window);
    QObject::connect(window, &QDialog::finished, window, &QObject::deleteLater);
    QObject::connect(window, &QObject::destroyed, this, [this, id](QObject* object)
    {
        const auto iterator = backgroundMusicWindows_.find(id.value);
        if (iterator != backgroundMusicWindows_.end() && iterator.value().data() == object)
        {
            backgroundMusicWindows_.erase(iterator);
        }
    });
    synchronizeBackgroundMusicPlaybackDisplay();
    applyBackgroundMusicPlaybackDisplayState(*window);
    window->show();
    return window;
}

void MainWindow::refreshOpenBackgroundMusicWindow(PlaylistId id)
{
    const auto iterator = backgroundMusicWindows_.find(id.value);
    if (iterator == backgroundMusicWindows_.end())
    {
        return;
    }

    if (auto* window = iterator.value().data())
    {
        window->refreshFromModel();
        return;
    }

    backgroundMusicWindows_.erase(iterator);
}

void MainWindow::refreshAllOpenBackgroundMusicWindows()
{
    auto iterator = backgroundMusicWindows_.begin();
    while (iterator != backgroundMusicWindows_.end())
    {
        if (auto* window = iterator.value().data())
        {
            window->refreshFromModel();
            ++iterator;
        }
        else
        {
            iterator = backgroundMusicWindows_.erase(iterator);
        }
    }
}

void MainWindow::closeBackgroundMusicWindow(PlaylistId id)
{
    const auto iterator = backgroundMusicWindows_.find(id.value);
    if (iterator == backgroundMusicWindows_.end())
    {
        return;
    }

    const QPointer<DesktopSceneEngine::Player::BackgroundMusicWindow> window = iterator.value();
    backgroundMusicWindows_.erase(iterator);
    if (!window.isNull())
    {
        window->close();
    }
}

void MainWindow::closeAllBackgroundMusicWindows()
{
    const auto windows = backgroundMusicWindows_.values();
    backgroundMusicWindows_.clear();
    for (const auto& window : windows)
    {
        if (!window.isNull())
        {
            QObject::disconnect(window, nullptr, this, nullptr);
            window->close();
        }
    }
}

MainWindow::BackgroundMusicPlaybackDisplayState
MainWindow::currentBackgroundMusicPlaybackDisplayState() const
{
    BackgroundMusicPlaybackDisplayState state{};
    if (!activePlaybackProjection_.has_value())
    {
        return state;
    }

    state.activePlaylistId = activePlaybackProjection_->sourcePlaylistId;
    state.activeItemId = activeTimelineItemId();
    state.playlistTrackId = engine_.currentPlaylistBackgroundMusicTrackId();
    state.itemTrackId = engine_.currentItemBackgroundMusicTrackId();
    return state;
}

void MainWindow::synchronizeBackgroundMusicPlaybackDisplay()
{
    const BackgroundMusicPlaybackDisplayState state = currentBackgroundMusicPlaybackDisplayState();
    if (state == backgroundMusicPlaybackDisplayState_)
    {
        return;
    }

    backgroundMusicPlaybackDisplayState_ = state;
    auto iterator = backgroundMusicWindows_.begin();
    while (iterator != backgroundMusicWindows_.end())
    {
        if (auto* window = iterator.value().data())
        {
            applyBackgroundMusicPlaybackDisplayState(*window);
            ++iterator;
        }
        else
        {
            iterator = backgroundMusicWindows_.erase(iterator);
        }
    }
}

void MainWindow::applyBackgroundMusicPlaybackDisplayState(
    DesktopSceneEngine::Player::BackgroundMusicWindow& window
) const
{
    const auto playlistTrackId = workspaceTrackIdFor(
        backgroundMusicPlaybackDisplayState_.activePlaylistId.has_value()
            ? workspaceModel_.playlistMusicQueue(
                *backgroundMusicPlaybackDisplayState_.activePlaylistId)
            : nullptr,
        backgroundMusicPlaybackDisplayState_.playlistTrackId);
    const auto itemTrackId = workspaceTrackIdFor(
        backgroundMusicPlaybackDisplayState_.activeItemId.has_value()
            ? workspaceModel_.itemMusicQueue(*backgroundMusicPlaybackDisplayState_.activeItemId)
            : nullptr,
        backgroundMusicPlaybackDisplayState_.itemTrackId);
    window.setPlaybackDisplayState(
        backgroundMusicPlaybackDisplayState_.activePlaylistId,
        backgroundMusicPlaybackDisplayState_.activeItemId,
        playlistTrackId,
        itemTrackId);
}

void MainWindow::showPlaylistContextMenu(const QPoint& position)
{
    if (playlistList_ == nullptr)
    {
        return;
    }

    QListWidgetItem* listItem = playlistList_->itemAt(position);
    if (listItem == nullptr)
    {
        return;
    }

    playlistList_->setCurrentItem(listItem);
    const PlaylistId id = playlistIdFor(playlistList_->currentItem());
    if (!id.isValid())
    {
        return;
    }

    QMenu menu(this);
    QAction* playAction = menu.addAction(tr("Play"));
    playAction->setEnabled(workspaceModel_.isPlaylistPlayable(id));
    menu.addSeparator();
    QAction* availabilityAction = menu.addAction(
        workspaceModel_.playlistEnabled(id) ? tr("Disable") : tr("Enable"));
    QAction* moveUpAction = menu.addAction(tr("Move Up"));
    QAction* moveDownAction = menu.addAction(tr("Move Down"));
    const auto playlistIterator = std::find_if(
        workspaceModel_.playlists().cbegin(),
        workspaceModel_.playlists().cend(),
        [id](const DesktopSceneEngine::Player::WorkspacePlaylist& playlist)
        {
            return playlist.id == id;
        });
    const auto& playlists = workspaceModel_.playlists();
    moveUpAction->setEnabled(
        playlistIterator != playlists.cend() && playlistIterator != playlists.cbegin());
    moveDownAction->setEnabled(
        playlistIterator != playlists.cend() && std::next(playlistIterator) != playlists.cend());
    menu.addSeparator();
    QAction* renameAction = menu.addAction(tr("Rename"));
    QAction* configureBackgroundMusicAction = menu.addAction(tr("Configure Background Music..."));
    QAction* deleteAction = menu.addAction(tr("Delete"));
    deleteAction->setEnabled(workspaceModel_.playlist(id) != nullptr);
    QObject::connect(playAction, &QAction::triggered, this, [this, id]
    {
        activatePlaylist(id);
    });
    QObject::connect(availabilityAction, &QAction::triggered, this, [this, id]
    {
        if (setPlaylistAvailability(id, !workspaceModel_.playlistEnabled(id)))
        {
            refreshLibrary();
            refreshStatus();
        }
    });
    QObject::connect(moveUpAction, &QAction::triggered, this, [this, id]
    {
        if (workspaceModel_.movePlaylist(id, -1))
        {
            refreshLibrary();
        }
    });
    QObject::connect(moveDownAction, &QAction::triggered, this, [this, id]
    {
        if (workspaceModel_.movePlaylist(id, 1))
        {
            refreshLibrary();
        }
    });
    QObject::connect(renameAction, &QAction::triggered, this, [this, id]
    {
        renamePlaylist(id);
    });
    QObject::connect(configureBackgroundMusicAction, &QAction::triggered, this, [this, id]
    {
        (void)openBackgroundMusicWindow(id);
    });
    QObject::connect(deleteAction, &QAction::triggered, this, [this, id]
    {
        removePlaylist(id);
    });
    (void)menu.exec(playlistList_->viewport()->mapToGlobal(position));
}

void MainWindow::showItemContextMenu(const QPoint& position)
{
    if (itemList_ == nullptr)
    {
        return;
    }

    QListWidgetItem* listItem = itemList_->itemAt(position);
    if (listItem == nullptr)
    {
        return;
    }

    itemList_->setCurrentItem(listItem);
    const ItemId id = itemIdFor(itemList_->currentItem());
    if (!id.isValid())
    {
        return;
    }

    const PlaylistId playlistId = workspaceModel_.selectedPlaylistId();
    const auto* playlist = workspaceModel_.playlist(playlistId);
    if (playlist == nullptr)
    {
        return;
    }

    const auto itemIterator = std::find_if(
        playlist->items.cbegin(),
        playlist->items.cend(),
        [id](const DesktopSceneEngine::Player::WorkspaceItem& item)
        {
            return item.id == id;
        });
    if (itemIterator == playlist->items.cend())
    {
        return;
    }

    QMenu menu(this);
    QAction* playAction = menu.addAction(tr("Play"));
    playAction->setEnabled(workspaceModel_.isItemPlayable(playlistId, id));
    menu.addSeparator();
    QAction* availabilityAction = menu.addAction(
        workspaceModel_.itemEnabled(id) ? tr("Disable") : tr("Enable"));
    availabilityAction->setEnabled(workspaceModel_.playlistEnabled(playlistId));
    QAction* moveUpAction = menu.addAction(tr("Move Up"));
    QAction* moveDownAction = menu.addAction(tr("Move Down"));
    moveUpAction->setEnabled(itemIterator != playlist->items.cbegin());
    moveDownAction->setEnabled(std::next(itemIterator) != playlist->items.cend());
    menu.addSeparator();
    QAction* renameAction = menu.addAction(tr("Rename"));
    QAction* deleteAction = menu.addAction(tr("Delete"));
    QObject::connect(playAction, &QAction::triggered, this, [this, id]
    {
        activateItem(id);
    });
    QObject::connect(availabilityAction, &QAction::triggered, this, [this, id]
    {
        if (setItemAvailability(id, !workspaceModel_.itemEnabled(id)))
        {
            refreshLibrary();
            refreshStatus();
        }
    });
    QObject::connect(moveUpAction, &QAction::triggered, this, [this, id]
    {
        if (workspaceModel_.moveItem(id, -1))
        {
            refreshLibrary();
        }
    });
    QObject::connect(moveDownAction, &QAction::triggered, this, [this, id]
    {
        if (workspaceModel_.moveItem(id, 1))
        {
            refreshLibrary();
        }
    });
    QObject::connect(renameAction, &QAction::triggered, this, [this, id]
    {
        renameItem(id);
    });
    QObject::connect(deleteAction, &QAction::triggered, this, [this, id]
    {
        removeItem(id);
    });
    (void)menu.exec(itemList_->viewport()->mapToGlobal(position));
}

void MainWindow::renamePlaylist(PlaylistId id)
{
    const auto* playlist = workspaceModel_.playlist(id);
    if (playlist == nullptr)
    {
        return;
    }

    bool accepted = false;
    const QString alias = QInputDialog::getText(
        this,
        tr("Rename Playlist"),
        tr("Name"),
        QLineEdit::Normal,
        playlist->displayAlias,
        &accepted);
    if (!accepted)
    {
        return;
    }

    if (!workspaceModel_.renamePlaylist(id, alias))
    {
        QMessageBox::warning(this, tr("Invalid Name"), tr("Name cannot be empty."));
        return;
    }
    refreshOpenBackgroundMusicWindow(id);
    refreshLibrary();
}

void MainWindow::removePlaylist(PlaylistId id)
{
    const auto* playlist = workspaceModel_.playlist(id);
    if (playlist == nullptr)
    {
        return;
    }

    const auto response = QMessageBox::question(
        this,
        tr("Delete Playlist"),
        tr("Delete playlist \"%1\"?").arg(playlist->displayAlias),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (response == QMessageBox::Yes)
    {
        if (activePlaybackProjection_.has_value()
            && activePlaybackProjection_->sourcePlaylistId == id)
        {
            stopActivePlayback();
        }

        closeBackgroundMusicWindow(id);
        if (workspaceModel_.removePlaylist(id))
        {
            refreshLibrary();
            refreshStatus();
        }
    }
}

void MainWindow::activatePlaylist(PlaylistId id)
{
    const auto* playlist = workspaceModel_.playlist(id);
    if (playlist == nullptr)
    {
        return;
    }

    if (playlist->items.isEmpty())
    {
        QMessageBox::information(
            this,
            tr("Empty Playlist"),
            tr("This playlist has no items to play."));
        return;
    }

    (void)startPlaylistPlayback(id, std::nullopt);
}

void MainWindow::renameItem(ItemId id)
{
    const auto* item = workspaceModel_.item(id);
    if (item == nullptr)
    {
        return;
    }

    const PlaylistId playlistId = workspaceModel_.selectedPlaylistId();
    bool accepted = false;
    const QString alias = QInputDialog::getText(
        this,
        tr("Rename Item"),
        tr("Name"),
        QLineEdit::Normal,
        item->displayAlias,
        &accepted);
    if (!accepted)
    {
        return;
    }

    if (!workspaceModel_.renameItem(id, alias))
    {
        QMessageBox::warning(this, tr("Invalid Name"), tr("Name cannot be empty."));
        return;
    }
    refreshOpenBackgroundMusicWindow(playlistId);
    refreshLibrary();
}

void MainWindow::removeItem(ItemId id)
{
    const PlaylistId playlistId = workspaceModel_.selectedPlaylistId();
    QMessageBox::StandardButton response;
    {
        const auto* item = workspaceModel_.item(id);
        if (item == nullptr)
        {
            return;
        }
        response = QMessageBox::question(
            this,
            tr("Remove Item"),
            tr("Remove \"%1\" from the playlist?\nThe source file will not be deleted.")
                .arg(item->displayAlias),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
    }
    if (response == QMessageBox::Yes)
    {
        const PlaylistId sourcePlaylistId = activePlaybackProjection_.has_value()
            ? activePlaybackProjection_->sourcePlaylistId
            : PlaylistId{};
        bool removesActivePlaylistItem = false;
        {
            const auto* activePlaylist = workspaceModel_.playlist(sourcePlaylistId);
            removesActivePlaylistItem = activePlaylist != nullptr
                && std::any_of(
                    activePlaylist->items.cbegin(),
                    activePlaylist->items.cend(),
                    [id](const auto& candidate)
                    {
                        return candidate.id == id;
                    });
        }
        const auto previousActiveItemId = activePlaybackProjection_.has_value()
            ? activePlaybackProjection_->itemForSceneIndex(engine_.activeSceneIndex())
            : std::nullopt;
        const bool removesCurrentItem = removesActivePlaylistItem
            && previousActiveItemId == id;

        std::optional<ItemId> successorItemId;
        if (removesCurrentItem)
        {
            // Deletion follows current playable authored order, not the bound
            // Engine scene index or the sequence navigation mode.
            const auto collections = playbackSequenceCollections();
            const auto playlist = std::find_if(
                collections.itemsByPlaylist.cbegin(),
                collections.itemsByPlaylist.cend(),
                [sourcePlaylistId](const auto& candidate)
                {
                    return candidate.playlistId == sourcePlaylistId;
                });
            if (playlist != collections.itemsByPlaylist.cend()
                && playlist->itemIds.size() > 1U)
            {
                const auto current = std::find(
                    playlist->itemIds.cbegin(), playlist->itemIds.cend(), id);
                if (current != playlist->itemIds.cend())
                {
                    auto next = current;
                    ++next;
                    if (next == playlist->itemIds.cend())
                    {
                        next = playlist->itemIds.cbegin();
                    }
                    // With at least two IDs, wrapping cannot select the deleted Item.
                    successorItemId = *next;
                }
            }
        }

        if (!workspaceModel_.removeItem(id))
        {
            return;
        }

        if (removesActivePlaylistItem)
        {
            const auto* updatedPlaylist = activePlaybackProjection_.has_value()
                ? workspaceModel_.playlist(activePlaybackProjection_->sourcePlaylistId)
                : nullptr;
            if (updatedPlaylist == nullptr || updatedPlaylist->items.isEmpty())
            {
                stopActivePlayback();
            }
            else if (removesCurrentItem)
            {
                if (!successorItemId.has_value()
                    || !workspaceModel_.isItemPlayable(sourcePlaylistId, *successorItemId))
                {
                    stopActivePlayback();
                }
                else
                {
                    const ActiveProjectionRebuildResult rebuildResult = rebuildActiveProjection(
                        *successorItemId,
                        ActiveProjectionRebuildMode::StartTargetItem);
                    if (rebuildResult != ActiveProjectionRebuildResult::StartedTarget)
                    {
                        stopActivePlayback();
                        QMessageBox::warning(
                            this,
                            tr("Unable to synchronize playback"),
                            tr("Playback was stopped because the updated playlist could not be synchronized."));
                    }
                    else
                    {
                        rebasePlaybackSequenceToActivePosition();
                    }
                }
            }
            else if (previousActiveItemId.has_value())
            {
                const ActiveProjectionRebuildResult rebuildResult = rebuildActiveProjection(
                    *previousActiveItemId,
                    ActiveProjectionRebuildMode::RestoreCurrentItem);
                if (rebuildResult == ActiveProjectionRebuildResult::ReboundAtStart)
                {
                    QMessageBox::warning(
                        this,
                        tr("Playlist updated"),
                        tr("Playlist updated, but the previous playback position could not be restored."));
                }
                else if (rebuildResult != ActiveProjectionRebuildResult::Restored)
                {
                    QMessageBox::warning(
                        this,
                        tr("Unable to synchronize playback"),
                        tr("Playlist changes could not be synchronized with active playback. "
                           "They will be applied the next time the playlist is played."));
                }
            }
            else
            {
                stopActivePlayback();
                QMessageBox::warning(
                    this,
                    tr("Unable to synchronize playback"),
                    tr("Playback was stopped because the updated playlist could not be synchronized."));
            }
        }

        refreshOpenBackgroundMusicWindow(playlistId);
        refreshLibrary();
        refreshStatus();
    }
}

void MainWindow::activateItem(ItemId id)
{
    if (workspaceModel_.item(id) != nullptr)
    {
        (void)startPlaylistPlayback(
            workspaceModel_.selectedPlaylistId(),
            id);
    }
}

bool MainWindow::setPlaylistAvailability(PlaylistId id, bool enabled)
{
    if (workspaceModel_.playlist(id) == nullptr)
    {
        return false;
    }

    if (workspaceModel_.playlistEnabled(id) == enabled)
    {
        return true;
    }

    if (!enabled && workspaceModel_.activePlaylistId() == id)
    {
        stopActivePlayback();
    }

    return workspaceModel_.setPlaylistEnabled(id, enabled);
}

bool MainWindow::setItemAvailability(ItemId id, bool enabled)
{
    if (workspaceModel_.item(id) == nullptr)
    {
        return false;
    }

    if (workspaceModel_.itemEnabled(id) == enabled)
    {
        return true;
    }

    if (!enabled && workspaceModel_.activeItemId() == id)
    {
        stopActivePlayback();
    }

    return workspaceModel_.setItemEnabled(id, enabled);
}

bool MainWindow::startPlaylistPlayback(
    PlaylistId playlistId,
    std::optional<ItemId> startItemId,
    bool synchronizeSelection
)
{
    if (!engineInitialized_)
    {
        return false;
    }

    if (!workspaceModel_.isPlaylistPlayable(playlistId))
    {
        return false;
    }

    const std::optional<ItemId> resolvedStartItemId = startItemId.has_value()
        ? startItemId
        : workspaceModel_.firstPlayableItem(playlistId);
    if (!resolvedStartItemId.has_value()
        || !workspaceModel_.isItemPlayable(playlistId, *resolvedStartItemId))
    {
        return false;
    }

    const auto* playlist = workspaceModel_.authoredPlaylist(playlistId);
    if (playlist == nullptr)
    {
        return false;
    }

    cancelTimelineDrag();

    auto candidate = DesktopSceneEngine::Player::makeActivePlaybackProjection(
        *playlist);
    if (!candidate.has_value())
    {
        QMessageBox::warning(
            this,
            tr("Unable to play media"),
            tr("Unable to create a playback sequence."));
        return false;
    }

    const std::size_t startSceneIndex = candidate->sceneIndexForItem(
        *resolvedStartItemId).value_or(candidate->project.scenes.size());
    if (startSceneIndex >= candidate->project.scenes.size())
    {
        QMessageBox::warning(
            this,
            tr("Unable to play media"),
            tr("Unable to create a playback sequence."));
        return false;
    }

    const DesktopSceneEngine::Domain::EntityId targetSceneId =
        candidate->project.scenes[startSceneIndex].id;
    const bool hadPreviousActivePlayback = activePlaybackProjection_.has_value();
    const auto previousBackgroundMusicScope = activeBackgroundMusicScopeSnapshot();
    std::optional<DesktopSceneEngine::Domain::EntityId> previousSceneId{};
    const bool previousPaused = engine_.isPaused();
    const float previousPlaybackSpeed = engine_.playbackSpeed();
    if (activePlaybackProjection_.has_value())
    {
        const std::size_t previousSceneIndex = engine_.activeSceneIndex();
        if (previousSceneIndex < activePlaybackProjection_->project.scenes.size())
        {
            previousSceneId = activePlaybackProjection_->project.scenes[
                previousSceneIndex].id;
        }
    }

    auto previousProjection = std::move(activePlaybackProjection_);
    engine_.detachProject();
    activePlaybackProjection_ = std::move(candidate);
    if (engine_.bindProject(activePlaybackProjection_->project, targetSceneId))
    {
        synchronizeActivePlaybackVolumeCluster();
        updateEffectiveOutputVolume();
        const auto targetItem = activePlaybackProjection_->itemForSceneIndex(
            startSceneIndex);
        if (targetItem.has_value())
        {
            if (synchronizeSelection)
            {
                (void)workspaceModel_.activateItem(*targetItem);
            }
            else
            {
                (void)workspaceModel_.setActiveItem(*targetItem);
            }
        }
        forwardBackgroundMusicScopeAfterStart(
            hadPreviousActivePlayback,
            previousBackgroundMusicScope);
        if (synchronizeSelection)
        {
            rebasePlaybackSequenceToActivePosition();
        }
        refreshLibrary();
        refreshStatus();
        return true;
    }

    activePlaybackProjection_.reset();
    if (previousProjection.has_value())
    {
        activePlaybackProjection_ = std::move(previousProjection);
        const DesktopSceneEngine::Domain::EntityId restoreSceneId =
            previousSceneId.has_value()
                ? *previousSceneId
                : activePlaybackProjection_->project.scenes.front().id;
        if (engine_.bindProject(activePlaybackProjection_->project, restoreSceneId))
        {
            engine_.setPlaybackSpeed(previousPlaybackSpeed);
            if (previousPaused)
            {
                engine_.togglePause();
            }
        }
        else
        {
            activePlaybackProjection_.reset();
        }
    }

    updateEffectiveOutputVolume();
    synchronizeActivePlaybackVolumeCluster();
    synchronizeActivePlaybackItem();
    refreshLibrary();
    refreshStatus();
    QMessageBox::warning(
        this,
        tr("Unable to play media"),
        tr("Media file could not be opened."));
    return false;
}

MainWindow::PlaybackSequence::Collections MainWindow::playbackSequenceCollections() const
{
    PlaybackSequence::Collections collections{};
    collections.playlistIds.reserve(static_cast<std::size_t>(workspaceModel_.playlists().size()));
    collections.itemsByPlaylist.reserve(static_cast<std::size_t>(workspaceModel_.playlists().size()));

    for (const DesktopSceneEngine::Player::WorkspacePlaylist& playlist
         : workspaceModel_.playlists())
    {
        if (!workspaceModel_.isPlaylistPlayable(playlist.id))
        {
            continue;
        }

        PlaybackSequence::PlaylistItems items{};
        items.playlistId = playlist.id;
        items.itemMode = playlist.itemPlaybackMode;
        items.itemIds.reserve(static_cast<std::size_t>(playlist.items.size()));
        for (const DesktopSceneEngine::Player::WorkspaceItem& item : playlist.items)
        {
            if (workspaceModel_.isItemPlayable(playlist.id, item.id))
            {
                items.itemIds.push_back(item.id);
            }
        }

        collections.playlistIds.push_back(playlist.id);
        collections.itemsByPlaylist.push_back(std::move(items));
    }

    return collections;
}

void MainWindow::synchronizePlaybackSequenceConfiguration(
    const PlaybackSequence::Collections& collections
)
{
    const auto configuredPlaylistMode = workspaceModel_.playlistPlaybackMode();
    if (playbackSequence_.playlistMode() != configuredPlaylistMode)
    {
        playbackSequence_.setPlaylistMode(configuredPlaylistMode, collections);
    }

    playbackSequence_.synchronizeItemMode(collections);
}

void MainWindow::rebasePlaybackSequenceToActivePosition()
{
    if (!activePlaybackProjection_.has_value())
    {
        playbackSequence_.clear();
        return;
    }

    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (!activeItemId.has_value())
    {
        playbackSequence_.clear();
        return;
    }

    const PlaybackSequence::Collections collections = playbackSequenceCollections();
    synchronizePlaybackSequenceConfiguration(collections);
    playbackSequence_.rebase(
        collections,
        activePlaybackProjection_->sourcePlaylistId,
        *activeItemId);
}

bool MainWindow::transitionToSequencePosition(const PlaybackSequence::Position& position)
{
    if (!activePlaybackProjection_.has_value())
    {
        return false;
    }

    const auto* targetPlaylist = workspaceModel_.playlist(position.playlistId);
    const auto* targetItem = workspaceModel_.item(position.itemId);
    if (targetPlaylist == nullptr || targetItem == nullptr)
    {
        return false;
    }

    if (activePlaybackProjection_->sourcePlaylistId != position.playlistId)
    {
        return startPlaylistPlayback(position.playlistId, position.itemId, false);
    }

    const auto targetSceneIndex = activePlaybackProjection_->sceneIndexForItem(position.itemId);
    if (!targetSceneIndex.has_value()
        || *targetSceneIndex >= activePlaybackProjection_->project.scenes.size())
    {
        return false;
    }

    const auto previousScope = activeBackgroundMusicScopeSnapshot();
    const DesktopSceneEngine::Domain::EntityId& targetSceneId =
        activePlaybackProjection_->project.scenes[*targetSceneIndex].id;
    if (!engine_.switchScene(activePlaybackProjection_->project, targetSceneId)
        || !workspaceModel_.setActiveItem(position.itemId))
    {
        return false;
    }

    invalidateTimelineHoverPreview();
    updateEffectiveOutputVolume();
    const auto currentScope = activeBackgroundMusicScopeSnapshot();
    if (previousScope.has_value() && currentScope.has_value()
        && !sameBackgroundMusicScope(*previousScope, *currentScope))
    {
        forwardBackgroundMusicScopeChange();
    }
    synchronizeActivePlaybackItem();
    refreshPlaylistList();
    refreshItemList();
    return true;
}

bool MainWindow::advancePlaybackSequence()
{
    if (!activePlaybackProjection_.has_value())
    {
        return false;
    }

    const PlaybackSequence::Collections collections = playbackSequenceCollections();
    synchronizePlaybackSequenceConfiguration(collections);
    if (!playbackSequence_.current().has_value())
    {
        rebasePlaybackSequenceToActivePosition();
    }

    const auto previousPosition = playbackSequence_.current();
    if (!previousPosition.has_value())
    {
        stopActivePlayback();
        return false;
    }

    const std::size_t maximumAttempts = (std::max)(
        std::size_t{1U},
        collections.playlistIds.size() * MaximumRandomRoundResolutionAttempts);
    for (std::size_t attempt = 0U; attempt < maximumAttempts; ++attempt)
    {
        const PlaybackSequence::Result result = playbackSequence_.completeCurrentItem(collections);
        if (result.kind == DesktopSceneEngine::Player::PlaybackSequenceResultKind::Identity
            && result.identity.has_value())
        {
            if (transitionToSequencePosition(*result.identity))
            {
                return true;
            }

            playbackSequence_.rebase(
                collections,
                previousPosition->playlistId,
                previousPosition->itemId);
            return false;
        }

        if (result.kind == DesktopSceneEngine::Player::PlaybackSequenceResultKind::NoTarget)
        {
            stopActivePlayback();
            return false;
        }
    }

    // A Random outer round may contain only now-empty Playlists. Its controller
    // retry is finite; exhausting this Player-level bound stops safely rather
    // than repeatedly regenerating random rounds on the UI thread.
    stopActivePlayback();
    return false;
}

std::optional<DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot>
MainWindow::activeBackgroundMusicScopeSnapshot() const
{
    if (!activePlaybackProjection_.has_value())
    {
        return std::nullopt;
    }

    const auto* const playlist = workspaceModel_.authoredPlaylist(
        activePlaybackProjection_->sourcePlaylistId);
    if (playlist == nullptr)
    {
        return std::nullopt;
    }

    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    const auto* const activeItem = activeItemId.has_value()
        ? workspaceModel_.authoredItem(*activeItemId)
        : nullptr;
    if (activeItemId.has_value() && activeItem == nullptr)
    {
        return std::nullopt;
    }

    return DesktopSceneEngine::Player::makeActiveBackgroundMusicScopeSnapshot(
        *playlist,
        activeItem);
}

bool MainWindow::forwardBackgroundMusicControl(
    const DesktopSceneEngine::BackgroundMusicControl::Event& event
)
{
    const bool accepted = backgroundMusicControlForwarder_
        ? backgroundMusicControlForwarder_(event)
        : engine_.processBackgroundMusicControl(event);
    if (!accepted)
    {
        std::cerr << "[Player] Background music control was rejected by Engine.\n";
    }

    return accepted;
}

void MainWindow::forwardBackgroundMusicScopeAfterStart(
    bool hadPreviousActivePlayback,
    const std::optional<DesktopSceneEngine::BackgroundMusicControl::ActiveScopeSnapshot>&
        previousScope
)
{
    const auto currentScope = activeBackgroundMusicScopeSnapshot();
    if (!currentScope.has_value())
    {
        std::cerr << "[Player] Active playback scope could not be converted for background music.\n";
        return;
    }

    if (!hadPreviousActivePlayback)
    {
        (void)forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaybackSessionStarted{*currentScope});
        return;
    }

    if (!previousScope.has_value()
        || !sameBackgroundMusicScope(*previousScope, *currentScope))
    {
        (void)forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged{*currentScope});
    }
}

void MainWindow::forwardBackgroundMusicScopeChange()
{
    const auto scope = activeBackgroundMusicScopeSnapshot();
    if (!scope.has_value())
    {
        std::cerr << "[Player] Active playback scope could not be converted for background music.\n";
        return;
    }

    (void)forwardBackgroundMusicControl(
        DesktopSceneEngine::BackgroundMusicControl::ActivePlaybackScopeChanged{*scope});
}

void MainWindow::handleBackgroundMusicQueueChanged(
    PlaylistId playlistId,
    std::optional<ItemId> itemId
)
{
    if (!activePlaybackProjection_.has_value()
        || activePlaybackProjection_->sourcePlaylistId != playlistId)
    {
        return;
    }

    const auto activeScope = activeBackgroundMusicScopeSnapshot();
    if (!activeScope.has_value())
    {
        std::cerr << "[Player] Active playback scope could not be converted for background music.\n";
        return;
    }

    if (!itemId.has_value())
    {
        const auto* const playlist = workspaceModel_.authoredPlaylist(playlistId);
        const auto snapshot = playlist != nullptr
            ? DesktopSceneEngine::Player::makeBackgroundMusicQueueSnapshot(
                playlist->globalMusicQueue)
            : std::nullopt;
        if (!snapshot.has_value())
        {
            std::cerr << "[Player] Playlist music queue could not be converted for background music.\n";
            return;
        }

        (void)forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicConfigChanged{
                activeScope->playlistId,
                *snapshot});
        return;
    }

    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (!activeItemId.has_value() || *activeItemId != *itemId)
    {
        return;
    }

    const auto* const item = workspaceModel_.authoredItem(*itemId);
    const auto publicItemId = item != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicItemId(item->id)
        : std::nullopt;
    const auto snapshot = item != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicQueueSnapshot(item->musicQueue)
        : std::nullopt;
    if (!publicItemId.has_value() || !snapshot.has_value())
    {
        std::cerr << "[Player] Item music queue could not be converted for background music.\n";
        return;
    }

    (void)forwardBackgroundMusicControl(
        DesktopSceneEngine::BackgroundMusicControl::ItemMusicConfigChanged{
            activeScope->playlistId,
            *publicItemId,
            *snapshot});
}

void MainWindow::handlePlaylistMusicEnabledChanged(PlaylistId playlistId, bool enabled)
{
    const auto* const queue = workspaceModel_.playlistMusicQueue(playlistId);
    if (queue == nullptr || queue->enabled == enabled)
    {
        refreshOpenBackgroundMusicWindow(playlistId);
        return;
    }

    if (workspaceModel_.setPlaylistMusicEnabled(playlistId, enabled))
    {
        handleBackgroundMusicQueueChanged(playlistId, std::nullopt);
    }
    refreshOpenBackgroundMusicWindow(playlistId);
}

void MainWindow::handleItemMusicEnabledChanged(
    PlaylistId playlistId,
    ItemId itemId,
    bool enabled
)
{
    const auto* const playlist = workspaceModel_.playlist(playlistId);
    const bool itemBelongsToPlaylist = playlist != nullptr
        && std::any_of(
            playlist->items.cbegin(),
            playlist->items.cend(),
            [itemId](const auto& item)
            {
                return item.id == itemId;
            });
    const auto* const queue = itemBelongsToPlaylist
        ? workspaceModel_.itemMusicQueue(itemId)
        : nullptr;
    if (queue == nullptr || queue->enabled == enabled)
    {
        refreshOpenBackgroundMusicWindow(playlistId);
        return;
    }

    if (workspaceModel_.setItemMusicEnabled(itemId, enabled))
    {
        handleBackgroundMusicQueueChanged(playlistId, itemId);
    }
    refreshOpenBackgroundMusicWindow(playlistId);
}

void MainWindow::handlePlaylistTrackSwitchRequested(PlaylistId playlistId, TrackId trackId)
{
    if (!activePlaybackProjection_.has_value()
        || activePlaybackProjection_->sourcePlaylistId != playlistId)
    {
        return;
    }

    const auto* const playlist = workspaceModel_.authoredPlaylist(playlistId);
    if (playlist == nullptr || !playlist->globalMusicQueue.enabled)
    {
        return;
    }
    const auto publicPlaylistId = playlist != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicPlaylistId(playlist->id)
        : std::nullopt;
    const auto publicTrackId = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(trackId);
    if (!publicPlaylistId.has_value() || !publicTrackId.has_value())
    {
        return;
    }

    if (forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaylistMusicTrackSwitchRequested{
                *publicPlaylistId,
                *publicTrackId}))
    {
        synchronizeBackgroundMusicPlaybackDisplay();
    }
}

void MainWindow::handleItemTrackSwitchRequested(
    PlaylistId playlistId,
    ItemId itemId,
    TrackId trackId
)
{
    if (!activePlaybackProjection_.has_value()
        || activePlaybackProjection_->sourcePlaylistId != playlistId
        || activeTimelineItemId() != itemId)
    {
        return;
    }

    const auto* const playlist = workspaceModel_.authoredPlaylist(playlistId);
    const auto* const item = workspaceModel_.authoredItem(itemId);
    if (item == nullptr || !item->musicQueue.enabled)
    {
        return;
    }
    const auto publicPlaylistId = playlist != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicPlaylistId(playlist->id)
        : std::nullopt;
    const auto publicItemId = item != nullptr
        ? DesktopSceneEngine::Player::makeBackgroundMusicItemId(item->id)
        : std::nullopt;
    const auto publicTrackId = DesktopSceneEngine::Player::makeBackgroundMusicTrackId(trackId);
    if (!publicPlaylistId.has_value() || !publicItemId.has_value() || !publicTrackId.has_value())
    {
        return;
    }

    if (forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::ItemMusicTrackSwitchRequested{
                *publicPlaylistId,
                *publicItemId,
                *publicTrackId}))
    {
        synchronizeBackgroundMusicPlaybackDisplay();
    }
}

void MainWindow::togglePlaybackPause()
{
    const bool wasPaused = engine_.isPaused();
    engine_.togglePause();
    const bool isPaused = engine_.isPaused();
    if (isPaused != wasPaused)
    {
        if (isPaused)
        {
            (void)forwardBackgroundMusicControl(
                DesktopSceneEngine::BackgroundMusicControl::PlaybackPaused{});
        }
        else
        {
            (void)forwardBackgroundMusicControl(
                DesktopSceneEngine::BackgroundMusicControl::PlaybackResumed{});
        }
    }

    refreshStatus();
}

MainWindow::ActiveProjectionRebuildResult
MainWindow::rebuildActiveProjection(
    ItemId targetItemId,
    ActiveProjectionRebuildMode mode
)
{
    if (!engineInitialized_ || !activePlaybackProjection_.has_value()
        || !targetItemId.isValid())
    {
        return ActiveProjectionRebuildResult::Unchanged;
    }

    const PlaylistId sourcePlaylistId = activePlaybackProjection_->sourcePlaylistId;
    const auto* playlist = workspaceModel_.authoredPlaylist(sourcePlaylistId);
    const auto* targetItem = workspaceModel_.authoredItem(targetItemId);
    const auto activeItemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (playlist == nullptr || targetItem == nullptr
        || (mode == ActiveProjectionRebuildMode::RestoreCurrentItem
            && activeItemId != targetItemId))
    {
        return ActiveProjectionRebuildResult::Unchanged;
    }

    auto preparedCandidate = DesktopSceneEngine::Player::prepareActivePlaybackProjection(
        *playlist,
        targetItem->id);
    if (!preparedCandidate.has_value())
    {
        if (mode == ActiveProjectionRebuildMode::StartTargetItem)
        {
            stopActivePlayback();
            return ActiveProjectionRebuildResult::Stopped;
        }

        return ActiveProjectionRebuildResult::Unchanged;
    }

    float playbackTime = 0.0F;
    float playbackSpeed = 1.0F;
    bool wasPaused = false;
    if (mode == ActiveProjectionRebuildMode::RestoreCurrentItem)
    {
        playbackTime = engine_.playbackTime();
        playbackSpeed = engine_.playbackSpeed();
        wasPaused = engine_.isPaused();
        const std::size_t previousSceneIndex = engine_.activeSceneIndex();
        if (previousSceneIndex >= activePlaybackProjection_->project.scenes.size())
        {
            return ActiveProjectionRebuildResult::Unchanged;
        }
    }

    cancelTimelineDrag();

    auto oldProjection = std::move(activePlaybackProjection_);
    engine_.detachProject();
    activePlaybackProjection_ = std::move(preparedCandidate->projection);

    const auto restoreBoundPlayback = [this, playbackSpeed, playbackTime, wasPaused]()
    {
        if (wasPaused)
        {
            // bindProject starts a configured scene playing. Pause before speed
            // or seek restoration so their audio submissions remain paused.
            engine_.togglePause();
        }
        engine_.setPlaybackSpeed(playbackSpeed);
        const bool restoredTime = engine_.seek(playbackTime);
        updateEffectiveOutputVolume();
        synchronizeActivePlaybackItem();
        refreshStatus();
        return restoredTime;
    };

    const DesktopSceneEngine::Domain::EntityId targetSceneId =
        activePlaybackProjection_->project.scenes[
            preparedCandidate->targetSceneIndex].id;
    if (engine_.bindProject(activePlaybackProjection_->project, targetSceneId))
    {
        if (mode == ActiveProjectionRebuildMode::StartTargetItem)
        {
            updateEffectiveOutputVolume();
            synchronizeActivePlaybackItem();
            refreshStatus();
            return ActiveProjectionRebuildResult::StartedTarget;
        }

        return restoreBoundPlayback()
            ? ActiveProjectionRebuildResult::Restored
            : ActiveProjectionRebuildResult::ReboundAtStart;
    }

    engine_.detachProject();
    activePlaybackProjection_.reset();

    if (mode == ActiveProjectionRebuildMode::StartTargetItem)
    {
        updateEffectiveOutputVolume();
        refreshPlaylistList();
        refreshItemList();
        refreshStatus();
        return ActiveProjectionRebuildResult::Stopped;
    }

    activePlaybackProjection_ = std::move(oldProjection);

    const auto rollbackSceneIndex = activePlaybackProjection_->sceneIndexForItem(targetItemId);
    if (!rollbackSceneIndex.has_value())
    {
        stopActivePlayback();
        refreshPlaylistList();
        refreshItemList();
        refreshStatus();
        return ActiveProjectionRebuildResult::Stopped;
    }

    const DesktopSceneEngine::Domain::EntityId rollbackSceneId =
        activePlaybackProjection_->project.scenes[*rollbackSceneIndex].id;
    if (!engine_.bindProject(activePlaybackProjection_->project, rollbackSceneId))
    {
        stopActivePlayback();
        refreshPlaylistList();
        refreshItemList();
        refreshStatus();
        return ActiveProjectionRebuildResult::Stopped;
    }

    (void)restoreBoundPlayback();
    return ActiveProjectionRebuildResult::Unchanged;
}

void MainWindow::stopActivePlayback() noexcept
{
    cancelTimelineDrag();
    invalidateTimelineHoverPreview();
    invalidateTimelinePreviewPreloadTarget();
    if (activePlaybackProjection_.has_value()
        || workspaceModel_.activePlaylistId().isValid())
    {
        (void)forwardBackgroundMusicControl(
            DesktopSceneEngine::BackgroundMusicControl::PlaybackStopped{});
    }
    engine_.detachProject();
    activePlaybackProjection_.reset();
    playbackSequence_.clear();
    workspaceModel_.clearActivePlayback();
    synchronizeBackgroundMusicPlaybackDisplay();
    synchronizeActivePlaybackVolumeCluster();
    updateEffectiveOutputVolume();
}

void MainWindow::synchronizeActivePlaybackItem()
{
    if (!activePlaybackProjection_.has_value())
    {
        invalidateTimelineHoverPreview();
        invalidateTimelinePreviewPreloadTarget();
        return;
    }

    const auto previewSource = activeTimelinePreviewSource();
    if (timelineHoverPreviewSource_.has_value()
        && (!previewSource.has_value() || *timelineHoverPreviewSource_ != *previewSource))
    {
        invalidateTimelineHoverPreview();
    }

    reconcileTimelinePreviewPreloadTarget();

    const auto itemId = activePlaybackProjection_->itemForSceneIndex(
        engine_.activeSceneIndex());
    if (!itemId.has_value() || workspaceModel_.activeItemId() == itemId)
    {
        return;
    }

    if (workspaceModel_.setActiveItem(*itemId))
    {
        updateEffectiveOutputVolume();
        forwardBackgroundMusicScopeChange();
        refreshPlaylistList();
        refreshItemList();
    }
}

void MainWindow::navigateScene(bool previous)
{
    if (!activePlaybackProjection_.has_value())
    {
        refreshStatus();
        return;
    }

    cancelTimelineDrag();
    if (!previous)
    {
        (void)advancePlaybackSequence();
        refreshStatus();
        return;
    }

    const PlaybackSequence::Collections collections = playbackSequenceCollections();
    synchronizePlaybackSequenceConfiguration(collections);
    if (!playbackSequence_.current().has_value())
    {
        rebasePlaybackSequenceToActivePosition();
    }

    const auto previousPosition = playbackSequence_.current();
    if (previousPosition.has_value())
    {
        const PlaybackSequence::Result result = playbackSequence_.previous(collections);
        if (result.kind == DesktopSceneEngine::Player::PlaybackSequenceResultKind::Identity
            && result.identity.has_value()
            && !transitionToSequencePosition(*result.identity))
        {
            playbackSequence_.rebase(
                collections,
                previousPosition->playlistId,
                previousPosition->itemId);
        }
    }

    refreshStatus();
}

void MainWindow::applyApplicationPreferences()
{
    applyHighlightCanvasBoundary(applicationPreferences_.highlightCanvasBoundary);
}

void MainWindow::applyHighlightCanvasBoundary(bool highlighted)
{
    if (highlightCanvasBoundaryAction_ != nullptr)
    {
        const QSignalBlocker blocker{highlightCanvasBoundaryAction_};
        highlightCanvasBoundaryAction_->setChecked(highlighted);
    }

    if (canvasFrame_ != nullptr)
    {
        canvasFrame_->setHighlighted(highlighted);
    }
}

void MainWindow::reloadCommittedLanguage()
{
    if (preferencesStore_ == nullptr) return;
    const auto language = preferencesStore_->reloadLanguage();
    if (!language.has_value())
    {
        std::cerr << "[Player] Unable to reload committed Language; keeping current UI.\n";
        return;
    }
    applyCommittedLanguage(*language);
}

void MainWindow::applyCommittedLanguage(UiLanguage language)
{
    if (applicationPreferences_.language == language && translationState_.activeLanguage() == language) return;
    applicationPreferences_.language = language; // Desired truth, even when effective translation falls back.
    static_cast<void>(translationState_.setLanguage(language));
    retranslateUi();
}

void MainWindow::projectCanvasOutsideMode()
{
    if (canvasOutsideOpaqueAction_ == nullptr || canvasOutsideTransparentAction_ == nullptr)
        return;
    const QSignalBlocker opaqueBlocker{canvasOutsideOpaqueAction_};
    const QSignalBlocker transparentBlocker{canvasOutsideTransparentAction_};
    canvasOutsideOpaqueAction_->setChecked(currentCanvasOutsideMode_ == CanvasOutsideMode::Opaque);
    canvasOutsideTransparentAction_->setChecked(
        currentCanvasOutsideMode_ == CanvasOutsideMode::Transparent);
}

void MainWindow::selectCanvasOutsideMode(CanvasOutsideMode mode)
{
    if ((mode != CanvasOutsideMode::Opaque && mode != CanvasOutsideMode::Transparent)
        || mode == currentCanvasOutsideMode_)
        return;
    currentCanvasOutsideMode_ = mode;
    projectCanvasOutsideMode();
    if (preferencesStore_ != nullptr
        && preferencesStore_->saveCanvasOutsideMode(displaySelection_.boundDisplayId(), mode)
            != QSettings::NoError)
    {
        QMessageBox::warning(this, tr("Desktop Background"),
            tr("The desktop background setting could not be saved. This session keeps the selected mode."));
    }
}

void MainWindow::retranslateUi()
{
    hideToTrayButton_->setToolTip(tr("Hide to System Tray"));
    hideToTrayButton_->setAccessibleName(tr("Hide to System Tray"));
    updateDocumentTitle();

    if (fileMenu_ != nullptr)
    {
        fileMenu_->setTitle(tr("File"));
    }
    if (projectMenu_ != nullptr)
    {
        projectMenu_->setTitle(tr("Project"));
    }
    if (playbackMenu_ != nullptr)
    {
        playbackMenu_->setTitle(tr("Playback"));
    }
    if (viewMenu_ != nullptr)
    {
        viewMenu_->setTitle(tr("View"));
    }

    if (newProjectAction_ != nullptr)
    {
        newProjectAction_->setText(tr("New Project"));
    }
    if (openProjectAction_ != nullptr)
    {
        openProjectAction_->setText(tr("Open Project"));
    }
    if (saveProjectAction_ != nullptr)
    {
        saveProjectAction_->setText(tr("Save Project"));
    }
    if (saveProjectAsAction_ != nullptr)
    {
        saveProjectAsAction_->setText(tr("Save Project As"));
    }
    if (exitAction_ != nullptr)
    {
        exitAction_->setText(tr("Exit"));
    }
    if (addPlaylistAction_ != nullptr)
    {
        addPlaylistAction_->setText(tr("Add Playlist"));
    }
    if (addMediaAction_ != nullptr)
    {
        addMediaAction_->setText(tr("Add Media"));
    }
    if (playPauseAction_ != nullptr)
    {
        playPauseAction_->setText(tr("Play / Pause"));
    }
    if (previousAction_ != nullptr)
    {
        previousAction_->setText(tr("Previous"));
    }
    if (nextAction_ != nullptr)
    {
        nextAction_->setText(tr("Next"));
    }
    if (resetLayoutAction_ != nullptr)
    {
        resetLayoutAction_->setText(tr("Reset Layout"));
    }
    if (highlightCanvasBoundaryAction_ != nullptr)
    {
        highlightCanvasBoundaryAction_->setText(tr("Highlight Canvas Boundary"));
    }
    if (desktopBackgroundMenu_ != nullptr)
        desktopBackgroundMenu_->setTitle(tr("Desktop Background"));
    if (canvasOutsideOpaqueAction_ != nullptr)
        canvasOutsideOpaqueAction_->setText(tr("Cover Entire Desktop Background"));
    if (canvasOutsideTransparentAction_ != nullptr)
        canvasOutsideTransparentAction_->setText(tr("Reveal Wallpaper in Uncovered Areas"));
    projectCanvasOutsideMode();

    if (libraryHeader_ != nullptr)
    {
        libraryHeader_->setText(tr("LIBRARY"));
    }
    if (playlistsHeader_ != nullptr)
    {
        playlistsHeader_->setText(tr("PLAYLISTS"));
    }
    if (itemsHeader_ != nullptr)
    {
        itemsHeader_->setText(tr("ITEMS"));
    }
    if (previewHeader_ != nullptr)
    {
        previewHeader_->setText(tr("PREVIEW"));
    }
    if (mapToLabel_ != nullptr)
    {
        mapToLabel_->setText(tr("Target Display"));
    }
    updateCanvasAspectUi();
    if (playlistPlaybackModeLabel_ != nullptr)
    {
        playlistPlaybackModeLabel_->setText(tr("Playback", "Playback mode control"));
    }
    if (itemPlaybackModeLabel_ != nullptr)
    {
        itemPlaybackModeLabel_->setText(tr("Playback", "Playback mode control"));
    }
    if (playlistPlaybackModeCombo_ != nullptr)
    {
        const QSignalBlocker blocker{playlistPlaybackModeCombo_};
        setPlaybackModeText(
            *playlistPlaybackModeCombo_,
            PlaybackSequenceMode::ListLoop,
            tr("List Loop"));
        setPlaybackModeText(
            *playlistPlaybackModeCombo_,
            PlaybackSequenceMode::Random,
            tr("Random"));
        setPlaybackModeText(
            *playlistPlaybackModeCombo_,
            PlaybackSequenceMode::RepeatCurrent,
            tr("Repeat Current"));
    }
    if (itemPlaybackModeCombo_ != nullptr)
    {
        const QSignalBlocker blocker{itemPlaybackModeCombo_};
        setPlaybackModeText(
            *itemPlaybackModeCombo_,
            PlaybackSequenceMode::ListLoop,
            tr("List Loop"));
        setPlaybackModeText(
            *itemPlaybackModeCombo_,
            PlaybackSequenceMode::Random,
            tr("Random"));
        setPlaybackModeText(
            *itemPlaybackModeCombo_,
            PlaybackSequenceMode::RepeatCurrent,
            tr("Repeat Current"));
    }
    if (identifyDisplayButton_ != nullptr)
    {
        identifyDisplayButton_->setText(tr("Identify"));
    }
    updateBoundDisplayUi();
    if (addPlaylistButton_ != nullptr)
    {
        addPlaylistButton_->setText(tr("+ Add Playlist"));
    }
    if (addMediaButton_ != nullptr)
    {
        addMediaButton_->setText(tr("+ Add Media"));
    }
    if (previousButton_ != nullptr)
    {
        previousButton_->setText(tr("Previous"));
    }
    if (nextButton_ != nullptr)
    {
        nextButton_->setText(tr("Next"));
    }
    if (volumeLabel_ != nullptr)
    {
        volumeLabel_->setText(tr("Volume"));
    }
    if (timelineHoverPopup_ != nullptr)
    {
        timelineHoverPopup_->setUnavailableText(tr("Preview unavailable"));
        timelineHoverPopup_->setPendingText(tr("Loading preview…"));
    }

    refreshLibrary();
    refreshAllOpenBackgroundMusicWindows();

    if (engineInitialized_)
    {
        refreshStatus();
    }
    else
    {
        if (playPauseButton_ != nullptr)
        {
            playPauseButton_->setText(tr("Play"));
        }
        if (muteButton_ != nullptr)
        {
            muteButton_->setText(tr("Mute"));
        }
    }
}

bool MainWindow::initializeEngine()
{
    if (engineInitialized_)
    {
        return true;
    }

    const DesktopSceneEngine::Engine::ExternalWindowTarget target{
        reinterpret_cast<void*>(renderWidget_->winId())};
    previewPresentationTargetId_ = engine_.initialize(target);
    if (!engine_.isInitialized() || !previewPresentationTargetId_.isValid())
    {
        return false;
    }

    engine_.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    engineInitialized_ = true;
    updateEffectiveOutputVolume();
    updateBoundDisplayUi();
    previousTick_ = std::chrono::steady_clock::now();
    tickTimer_.start(16);
    refreshStatus();
    return true;
}

MainWindow::DesktopDeploymentAttempt MainWindow::tryDeployDesktopPresentation()
{
    using DesktopSceneEngine::Platform::DesktopPresentationHostStatus;
    try
    {
        const auto status = desktopHostCreatorForTesting_
            ? desktopHostCreatorForTesting_(desktopHost_, displaySelection_.boundDisplayId())
            : desktopHost_.create(displaySelection_.boundDisplayId());
        if (status != DesktopPresentationHostStatus::Ready)
        {
            std::cerr << "[Player] Desktop unavailable: host status=" << static_cast<int>(status) << '\n';
            return status == DesktopPresentationHostStatus::DisplayUnavailable
                    || status == DesktopPresentationHostStatus::TransientlyUnavailable
                ? DesktopDeploymentAttempt::TransientlyUnavailable
                : DesktopDeploymentAttempt::Failed;
        }
        desktopPresentationTargetId_ = engine_.attachPresentationTarget({
            desktopHost_.nativeHandle(), DesktopSceneEngine::PresentationTransport::DirectCompositionSwapChain});
        if (!desktopPresentationTargetId_.isValid())
        {
            std::cerr << "[Player] Desktop unavailable: graphics attachment failed.\n";
            (void)removeDesktopPresentation();
            return DesktopDeploymentAttempt::Failed;
        }
        if (!engine_.setPresentationPacingTarget(desktopPresentationTargetId_))
        {
            std::cerr << "[Player] Desktop unavailable: pacing selection failed.\n";
            (void)removeDesktopPresentation();
            return DesktopDeploymentAttempt::Failed;
        }
        std::cout << "[Player] Desktop ready: HWND=" << desktopHost_.nativeHandle()
            << " Engine=" << &engine_ << "; new target attached, Desktop selected as pacer." << std::endl;
        return DesktopDeploymentAttempt::Ready;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[Player] Desktop unavailable: " << error.what() << '\n';
        (void)removeDesktopPresentation();
    }
    return DesktopDeploymentAttempt::Failed;
}

bool MainWindow::removeDesktopPresentation() noexcept
{
    if (desktopPresentationTargetId_.isValid())
    {
        if (!engine_.detachPresentationTarget(desktopPresentationTargetId_))
        {
            // Retain HWND and ID until Engine can release the binding. Never
            // destroy a potentially still-borrowed window to hide a failure.
            std::cerr << "[Player] Desktop detach failed; host retained.\n";
            return false;
        }
        desktopPresentationTargetId_ = {};
        std::cout << "[Player] Desktop target detached before host reset." << std::endl;
    }
    if (!desktopHost_.destroy())
    {
        std::cerr << "[Player] Desktop host reset failed; ownership retained.\n";
        return false;
    }
    return true;
}

bool MainWindow::reconcileDesktopPresentation(bool allowContinuation)
{
    if (!engineInitialized_ || desktopReplacementInProgress_) return false;
    bool deployed = false;
    bool cleanupSucceeded = true;
    DesktopDeploymentAttempt attempt = DesktopDeploymentAttempt::Failed;
    {
        QScopedValueRollback<bool> replacing(desktopReplacementInProgress_, true);
        refreshDisplaySelection();
        const auto previousTarget = desktopPresentationTargetId_;
        if (!desktopDeploymentDesired_)
        {
            cleanupSucceeded = removeDesktopPresentation();
        }
        else if (previousTarget.isValid() && desktopHost_.isValid())
        {
            const auto attachment = desktopHostReconcilerForTesting_
                ? desktopHostReconcilerForTesting_(desktopHost_, displaySelection_.boundDisplayId())
                : desktopHost_.reconcileAttachment(displaySelection_.boundDisplayId());
            if (attachment == DesktopSceneEngine::Platform::DesktopPresentationHostAttachmentStatus::Ready)
            {
                deployed = true;
                attempt = DesktopDeploymentAttempt::Ready;
            }
            else
            {
                std::cout << "[Player] Desktop attachment invalidated: HWND="
                    << desktopHost_.nativeHandle() << " status=" << static_cast<int>(attachment) << std::endl;
                cleanupSucceeded = removeDesktopPresentation();
            }
        }
        else if (desktopDeploymentDesired_
            && (previousTarget.isValid() || desktopHost_.isValid()))
        {
            std::cout << "[Player] Desktop endpoint ownership mismatch; removing retained state." << std::endl;
            cleanupSucceeded = removeDesktopPresentation();
        }
        if (desktopDeploymentDesired_ && cleanupSucceeded && !deployed)
        {
            attempt = tryDeployDesktopPresentation();
            deployed = attempt == DesktopDeploymentAttempt::Ready;
        }
        if (deployed && previousTarget.isValid()
            && desktopPresentationTargetId_ != previousTarget)
            std::cout << "[Player] Desktop replacement: fresh target="
                << (desktopPresentationTargetId_ != previousTarget)
                << "; same Engine=" << &engine_ << std::endl;
    }
    if (!deployed && attempt == DesktopDeploymentAttempt::TransientlyUnavailable
        && allowContinuation && desktopDeploymentDesired_
        && !desktopEndpointReconciliationQueued_)
        desktopReconciliationContinuationTimer_.start();
    updateDesktopDeploymentButton();
    return desktopDeploymentDesired_ ? deployed : cleanupSucceeded;
}

void MainWindow::requestDesktopPresentationReconciliation()
{
    if (!engineInitialized_) return;
    desktopReconciliationContinuationTimer_.stop();
    if (desktopEndpointReconciliationQueued_) return;
    desktopEndpointReconciliationQueued_ = true;
    QMetaObject::invokeMethod(this, [this]
    {
        if (!desktopEndpointReconciliationQueued_) return;
        desktopEndpointReconciliationQueued_ = false;
        if (!engineInitialized_) return;
        (void)reconcileDesktopPresentation(true);
    }, Qt::QueuedConnection);
}

void MainWindow::runDesktopPresentationContinuation()
{
    desktopReconciliationContinuationTimer_.stop();
    if (!engineInitialized_ || !desktopDeploymentDesired_
        || desktopEndpointReconciliationQueued_ || desktopReplacementInProgress_)
        return;
    (void)reconcileDesktopPresentation(false);
}

void MainWindow::cancelDesktopPresentationReconciliation() noexcept
{
    desktopEndpointReconciliationQueued_ = false;
    desktopReconciliationContinuationTimer_.stop();
}

void MainWindow::shutdownEngine() noexcept
{
    tickTimer_.stop();
    const bool wasInitialized = engineInitialized_;
    const bool hadDesktopDeployment = desktopDeploymentDesired_ || desktopPresentationTargetId_.isValid()
        || desktopHost_.nativeHandle() != nullptr;
    engineInitialized_ = false; // Also excludes native events throughout shutdown.
    desktopDeploymentDesired_ = false;
    cancelDesktopPresentationReconciliation();
    updateBoundDisplayUi();
    const bool desktopRemoved = !hadDesktopDeployment || removeDesktopPresentation();
    invalidateTimelineHoverPreview();
    shutdownTimelinePreviewWorker();
    if (wasInitialized)
    {
        if (activePlaybackProjection_.has_value())
        {
            stopActivePlayback();
        }
        else
        {
            engine_.detachProject();
        }
    }
    activePlaybackProjection_.reset();
    playbackSequence_.clear();
    if (previewPresentationTargetId_.isValid())
    {
        (void)engine_.detachPresentationTarget(previewPresentationTargetId_);
        previewPresentationTargetId_ = {};
    }
    engine_.shutdown();
    if (!desktopRemoved)
    {
        // Exceptional detach failure: Engine shutdown released all graphics
        // before a final host reset. This does not recreate Engine/device state.
        desktopPresentationTargetId_ = {};
        (void)desktopHost_.destroy();
    }
    updateDesktopDeploymentButton();
}

std::optional<DesktopSceneEngine::CanvasOutsideInstruction>
MainWindow::canvasOutsideInstructionForFrame() const noexcept
{
    if (!desktopPresentationTargetId_.isValid())
        return std::nullopt;
    return DesktopSceneEngine::CanvasOutsideInstruction{
        desktopPresentationTargetId_, currentCanvasOutsideMode_};
}

void MainWindow::onTick()
{
    if (!engineInitialized_ || desktopReplacementInProgress_
        || (desktopPresentationTargetId_.isValid() && !desktopHost_.isValid()))
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float deltaTime = (std::min)(
        std::chrono::duration<float>(now - previousTick_).count(),
        0.1F);
    previousTick_ = now;

    engine_.update(deltaTime);
    if (engine_.consumeNaturalSceneCompletion())
    {
        (void)advancePlaybackSequence();
    }
    synchronizeActivePlaybackItem();
    synchronizeBackgroundMusicPlaybackDisplay();
    const auto canvasAspect = workspaceModel_.desktopCanvas().aspectRatio;
    const auto canvasOutside = canvasOutsideInstructionForFrame();
    engine_.render(canvasAspect, canvasOutside);

    statusElapsed_ += deltaTime;
    if (statusElapsed_ >= 0.1F)
    {
        statusElapsed_ = 0.0F;
        refreshStatus();
    }
}

void MainWindow::refreshStatus()
{
    if (!engineInitialized_)
    {
        return;
    }

    if (currentPlaybackRateValueLabel_ != nullptr)
    {
        currentPlaybackRateValueLabel_->setText(
            QString::number(engine_.playbackSpeed(), 'f', 2));
    }

    const std::size_t sceneIndex = engine_.activeSceneIndex();
    const std::size_t sceneCount = engine_.sceneCount();
    if (sceneCount == 0U)
    {
        cancelTimelineDrag();
        if (transportTimeLabel_ != nullptr)
        {
            transportTimeLabel_->setText(QStringLiteral("00:00 / 00:00"));
        }
        if (positionSlider_ != nullptr)
        {
            const QSignalBlocker blocker{positionSlider_};
            positionSlider_->setEnabled(false);
            positionSlider_->setValue(0);
        }
        statusBar()->showMessage(tr("No media loaded"));
        setPlaybackButtonText();
        if (muteButton_ != nullptr)
        {
            muteButton_->setText(
                engine_.isMuted() ? tr("Unmute") : tr("Mute"));
        }
        return;
    }

    const bool validScene = sceneIndex != (std::numeric_limits<std::size_t>::max)()
        && sceneIndex < sceneCount;
    const float effectiveDuration = engine_.effectiveDuration();
    const bool validTimeline = validScene
        && DesktopSceneEngine::Player::hasValidTimelineDuration(effectiveDuration);
    if (!validTimeline)
    {
        invalidateTimelineHoverPreview();
    }
    if (timelineDragging_ && !timelineDragTargetsCurrentScene(effectiveDuration))
    {
        cancelTimelineDrag();
    }

    const float displayedDuration = validTimeline ? effectiveDuration : 0.0F;
    const float displayedTime = timelineDragging_
        ? DesktopSceneEngine::Player::timelineTimeForValue(
            positionSlider_ != nullptr ? positionSlider_->value() : 0,
            timelineDragDuration_)
        : (validTimeline ? engine_.playbackTime() : 0.0F);
    const std::string position = DesktopSceneEngine::Player::formatTimelineTime(
        displayedTime);
    const std::string duration = DesktopSceneEngine::Player::formatTimelineTime(
        displayedDuration);
    QString mediaKind{};
    switch (engine_.mediaKind())
    {
    case DesktopSceneEngine::Engine::MediaKind::Image:
        mediaKind = tr("Image");
        break;
    case DesktopSceneEngine::Engine::MediaKind::Video:
        mediaKind = tr("Video");
        break;
    default:
        mediaKind = tr("Unknown");
        break;
    }

    if (transportTimeLabel_ != nullptr)
    {
        transportTimeLabel_->setText(
            QString::fromStdString(position + " / " + duration));
    }
    if (positionSlider_ != nullptr)
    {
        const QSignalBlocker blocker{positionSlider_};
        positionSlider_->setEnabled(validTimeline);
        if (!validTimeline)
        {
            positionSlider_->setValue(0);
        }
        else if (!timelineDragging_)
        {
            positionSlider_->setValue(
                DesktopSceneEngine::Player::timelineValueForTime(
                    engine_.playbackTime(),
                    effectiveDuration));
        }
    }

    QString status = tr("Preview") + QStringLiteral("  ·  ");
    const auto projectedItem = activePlaybackProjection_.has_value()
        ? activePlaybackProjection_->itemForSceneIndex(sceneIndex)
        : std::nullopt;
    const auto* projectedPlaylist = activePlaybackProjection_.has_value()
        ? workspaceModel_.playlist(activePlaybackProjection_->sourcePlaylistId)
        : nullptr;
    if (validScene && projectedItem.has_value() && projectedPlaylist != nullptr)
    {
        status += projectedPlaylist->displayAlias + QStringLiteral("  ·  ")
            + tr("Item") + QStringLiteral(" ")
            + QString::number(sceneIndex + 1) + QStringLiteral(" / ")
            + QString::number(sceneCount);
    }
    else
    {
        status += tr("Scene") + QStringLiteral(" ");
        if (validScene)
        {
            status += QString::number(sceneIndex + 1) + QStringLiteral(" / ")
                + QString::number(sceneCount);
        }
        else
        {
            status += QStringLiteral("-- / ") + QString::number(sceneCount);
        }
    }
    status += QStringLiteral("  ·  ") + mediaKind
        + QStringLiteral("  ·  ")
        + (engine_.isPaused() ? tr("Paused") : tr("Playing"))
        + QStringLiteral("  ·  ") + QString::fromStdString(position)
        + QStringLiteral(" / ") + QString::fromStdString(duration)
        + QStringLiteral("  ·  ")
        + QString::number(engine_.playbackSpeed(), 'f', 1) + tr("x")
        + QStringLiteral("  ·  ") + tr("Volume") + QStringLiteral(" ")
        + QString::number(activePlaylistMasterVolume() * 100.0F, 'f', 0)
        + QStringLiteral("%");
    if (engine_.isMuted())
    {
        status += QStringLiteral(" (") + tr("Muted") + QStringLiteral(")");
    }
    statusBar()->showMessage(status);
    setPlaybackButtonText();
    if (muteButton_ != nullptr)
    {
        muteButton_->setText(
            engine_.isMuted() ? tr("Unmute") : tr("Mute"));
    }
}

std::optional<ItemId> MainWindow::activeTimelineItemId() const noexcept
{
    if (!activePlaybackProjection_.has_value())
    {
        return std::nullopt;
    }

    return activePlaybackProjection_->itemForSceneIndex(engine_.activeSceneIndex());
}

std::optional<DesktopSceneEngine::Player::TimelinePreviewSource>
MainWindow::activeTimelinePreviewSource() const
{
    if (!activePlaybackProjection_.has_value())
    {
        return std::nullopt;
    }

    return DesktopSceneEngine::Player::timelinePreviewSourceForActiveScene(
        workspaceModel_,
        *activePlaybackProjection_,
        engine_.activeSceneIndex());
}

const DesktopSceneEngine::Domain::Scene* MainWindow::activeTimelinePreviewScene() const noexcept
{
    if (!activePlaybackProjection_.has_value())
    {
        return nullptr;
    }

    const std::size_t sceneIndex = engine_.activeSceneIndex();
    return sceneIndex < activePlaybackProjection_->project.scenes.size()
        ? &activePlaybackProjection_->project.scenes[sceneIndex]
        : nullptr;
}

bool MainWindow::reconcileTimelineHoverPreviewSource(
    const DesktopSceneEngine::Player::TimelinePreviewSource& source
)
{
    if (timelineHoverPreviewSource_.has_value()
        && *timelineHoverPreviewSource_ == source)
    {
        return source.mediaType == WorkspaceMediaType::Image
            ? timelinePreviewProvider_.hasSource()
            : true;
    }

    timelineHoverPreviewTimer_.stop();
    pendingTimelineHoverPreview_.reset();
    timelinePreviewRequestState_.invalidatePending();
    ++timelineHoverPreviewGeneration_;
    timelineHoverPreviewSource_ = source;
    displayedTimelineHoverPreview_.reset();

    timelinePreviewProvider_.clear();
    const bool loaded = source.mediaType == WorkspaceMediaType::Image
        ? timelinePreviewProvider_.setSource(
            source.itemId,
            source.source,
            source.mediaType)
        : true;
    clearTimelinePreviewWorkerSource();
    setTimelineHoverPreviewUnavailable();

    return loaded;
}

void MainWindow::updateTimelineHoverPreview(const QPoint& pointInSlider)
{
    if (positionSlider_ == nullptr || !positionSlider_->isEnabled())
    {
        invalidateTimelineHoverPreview();
        return;
    }

    const float duration = engine_.effectiveDuration();
    const auto source = activeTimelinePreviewSource();
    const auto durationSnapshot = durationMilliseconds(duration);
    if (!DesktopSceneEngine::Player::hasValidTimelineDuration(duration)
        || !source.has_value() || !durationSnapshot.has_value())
    {
        invalidateTimelineHoverPreview();
        return;
    }

    (void)reconcileTimelineHoverPreviewSource(*source);

    QStyleOptionSlider option{};
    option.initFrom(positionSlider_);
    option.orientation = positionSlider_->orientation();
    option.minimum = positionSlider_->minimum();
    option.maximum = positionSlider_->maximum();
    option.sliderPosition = positionSlider_->sliderPosition();
    option.sliderValue = positionSlider_->value();
    option.upsideDown = positionSlider_->invertedAppearance();
    const QRect groove = positionSlider_->style()->subControlRect(
        QStyle::CC_Slider,
        &option,
        QStyle::SC_SliderGroove,
        positionSlider_);
    const int sliderValue = DesktopSceneEngine::Player::timelineHoverValueForGroovePosition(
        pointInSlider.x(),
        groove,
        positionSlider_->minimum(),
        positionSlider_->maximum(),
        option.upsideDown);
    const float timeSeconds = DesktopSceneEngine::Player::timelineTimeForValue(
        sliderValue,
        duration);
    const DesktopSceneEngine::Player::TimelinePreviewTiming::TimePoint hoverEventTime =
        DesktopSceneEngine::Player::TimelinePreviewTiming::Clock::now();

    timelineHoverActive_ = true;
    timelineHoverPoint_ = pointInSlider;
    timelineHoverRawTimeSeconds_ = timeSeconds;
    lastTimelineHoverEvent_ = hoverEventTime;
    ++timelineHoverEventCount_;
    refreshTimelineHoverPreviewPresentation();
    if (timelineHoverPopup_ != nullptr)
    {
        timelineHoverPopup_->setTimestamp(QString::fromStdString(
            DesktopSceneEngine::Player::formatTimelineTime(timeSeconds)));
        timelineHoverPopup_->show();
        updateTimelineHoverPopupGeometry();
        timelineHoverPopup_->raise();
    }

    if (source->mediaType == WorkspaceMediaType::Image)
    {
        if (displayedTimelineHoverPreview_.has_value()
            && DesktopSceneEngine::Player::isTimelinePreviewFrameCurrent(
                displayedTimelineHoverPreview_->identity,
                timelinePreviewIdentity(
                    *source,
                    timeSeconds,
                    timelineHoverPreviewGeneration_,
                    displayedTimelineHoverPreview_->identity.identity.serial),
                timelineHoverPreviewPresentationGeneration_))
        {
            return;
        }

        PendingTimelineHoverPreview request{};
        request.source = *source;
        request.timeSeconds = timeSeconds;
        request.durationMilliseconds = *durationSnapshot;
        request.generation = timelineHoverPreviewGeneration_;
        request.serial = ++timelineHoverPreviewRequestSerial_;
        request.timing.hoverEvent = hoverEventTime;
        pendingTimelineHoverPreview_ = request;
        displayedTimelineHoverPreview_.reset();
        setTimelineHoverPreviewPending();
        if (!timelineHoverPreviewTimer_.isActive())
        {
            timelineHoverPreviewTimer_.start();
        }
        return;
    }

    const std::int64_t rawHoverMilliseconds = static_cast<std::int64_t>(std::llround(
        static_cast<double>(timeSeconds) * 1'000.0));
    const std::optional<std::int64_t> representativeTimestamp =
        DesktopSceneEngine::Player::representativeTimestampForTimelineHover(
            *durationSnapshot,
            rawHoverMilliseconds);
    if (!representativeTimestamp.has_value())
    {
        setTimelineHoverPreviewUnavailable();
        return;
    }

    const DesktopSceneEngine::Player::TimelinePreviewImageIdentity requestedIdentity{
        *source,
        *representativeTimestamp};
    std::optional<DesktopSceneEngine::Player::TimelinePreviewImageIdentity> displayedIdentity{};
    if (displayedTimelineHoverPreview_.has_value()
        && displayedTimelineHoverPreview_->identity.identity
               .representativeTimestampMilliseconds.has_value())
    {
        displayedIdentity = DesktopSceneEngine::Player::TimelinePreviewImageIdentity{
            displayedTimelineHoverPreview_->identity.identity.source,
            *displayedTimelineHoverPreview_->identity.identity
                 .representativeTimestampMilliseconds};
    }

    std::optional<DesktopSceneEngine::Player::TimelinePreviewImageIdentity> pendingIdentity{};
    if (pendingTimelineHoverPreview_.has_value()
        && pendingTimelineHoverPreview_->representativeTimestampMilliseconds.has_value())
    {
        pendingIdentity = DesktopSceneEngine::Player::TimelinePreviewImageIdentity{
            pendingTimelineHoverPreview_->source,
            *pendingTimelineHoverPreview_->representativeTimestampMilliseconds};
    }

    const DesktopSceneEngine::Player::TimelinePreviewImageRequestDecision decision =
        DesktopSceneEngine::Player::decideTimelinePreviewImageRequest(
            requestedIdentity,
            displayedIdentity,
            pendingIdentity);
    if (decision.shouldCancelPending && pendingTimelineHoverPreview_.has_value())
    {
        const bool wasWaitingForDebounce = !pendingTimelineHoverPreview_->submittedToWorker;
        if (pendingTimelineHoverPreview_->submittedToWorker)
        {
            timelinePreviewRequestState_.invalidatePending();
        }
        pendingTimelineHoverPreview_.reset();
        ++timelineHoverPreviewRequestSerial_;
        if (wasWaitingForDebounce && !decision.shouldSubmit)
        {
            timelineHoverPreviewTimer_.stop();
        }
    }

    if (!decision.shouldSubmit)
    {
        return;
    }

    PendingTimelineHoverPreview request{};
    request.source = *source;
    request.timeSeconds = timeSeconds;
    request.durationMilliseconds = *durationSnapshot;
    request.generation = timelineHoverPreviewGeneration_;
    request.serial = ++timelineHoverPreviewRequestSerial_;
    request.representativeTimestampMilliseconds = representativeTimestamp;
    request.timing.hoverEvent = hoverEventTime;
    pendingTimelineHoverPreview_ = request;
    if (!decision.retainDisplayedImage)
    {
        setTimelineHoverPreviewPending();
    }
    if (!timelineHoverPreviewTimer_.isActive())
    {
        timelineHoverPreviewTimer_.start();
    }
}

void MainWindow::decodeTimelineHoverPreview()
{
    if (!pendingTimelineHoverPreview_.has_value() || timelineHoverPopup_ == nullptr)
    {
        return;
    }

    if (pendingTimelineHoverPreview_->submittedToWorker)
    {
        return;
    }

    const PendingTimelineHoverPreview request = *pendingTimelineHoverPreview_;
    const auto activeSource = activeTimelinePreviewSource();
    if (request.generation != timelineHoverPreviewGeneration_
        || !activeSource.has_value() || *activeSource != request.source
        || !timelineHoverPreviewSource_.has_value()
        || *timelineHoverPreviewSource_ != request.source)
    {
        pendingTimelineHoverPreview_.reset();
        return;
    }

    if (request.source.mediaType == WorkspaceMediaType::Image)
    {
        pendingTimelineHoverPreview_.reset();
        DesktopSceneEngine::Player::TimelinePreviewResult result{};
        result.request = {request.source,
                          request.timeSeconds,
                          request.durationMilliseconds,
                          request.generation,
                          request.serial,
                          request.representativeTimestampMilliseconds,
                          request.timing};
        result.image = timelinePreviewProvider_.frameAt(request.timeSeconds);
        applyTimelineHoverPreviewResult(result);
        return;
    }

    if (timelinePreviewWorker_ == nullptr || !timelinePreviewWorkerThread_.isRunning())
    {
        pendingTimelineHoverPreview_.reset();
        setTimelineHoverPreviewUnavailable();
        return;
    }

    DesktopSceneEngine::Player::TimelinePreviewRequest workerRequest{};
    workerRequest.source = request.source;
    workerRequest.timeSeconds = request.timeSeconds;
    workerRequest.durationMilliseconds = request.durationMilliseconds;
    workerRequest.generation = request.generation;
    workerRequest.serial = request.serial;
    workerRequest.representativeTimestampMilliseconds =
        request.representativeTimestampMilliseconds;
    workerRequest.timing = request.timing;
    workerRequest.timing.requestSubmitted =
        DesktopSceneEngine::Player::TimelinePreviewTiming::Clock::now();
    pendingTimelineHoverPreview_->timing = workerRequest.timing;
    pendingTimelineHoverPreview_->submittedToWorker = true;
    ++timelineHoverVideoRequestSubmissionCount_;
    if (timelinePreviewRequestState_.submit(workerRequest))
    {
        (void)QMetaObject::invokeMethod(
            timelinePreviewWorker_,
            &DesktopSceneEngine::Player::TimelinePreviewWorker::processLatest,
            Qt::QueuedConnection);
    }
}

void MainWindow::applyTimelineHoverPreviewResult(
    const DesktopSceneEngine::Player::TimelinePreviewResult& result
)
{
    if (timelineHoverPopup_ == nullptr || !timelineHoverPreviewSource_.has_value())
    {
        return;
    }

    const auto activeSource = activeTimelinePreviewSource();
    const DesktopSceneEngine::Player::TimelinePreviewResultContext context{
        timelineHoverActive_,
        activeSource,
        timelineHoverPreviewGeneration_,
        timelineHoverPreviewRequestSerial_};
    if (*timelineHoverPreviewSource_ != result.request.source
        || !DesktopSceneEngine::Player::shouldApplyTimelinePreviewResult(
            result.request,
            context))
    {
        timelinePreviewRequestState_.recordStaleResult();
        return;
    }

    if (pendingTimelineHoverPreview_.has_value()
        && pendingTimelineHoverPreview_->source == result.request.source
        && pendingTimelineHoverPreview_->generation == result.request.generation
        && pendingTimelineHoverPreview_->serial == result.request.serial)
    {
        pendingTimelineHoverPreview_.reset();
    }

    DesktopSceneEngine::Player::TimelinePreviewTiming timing = result.timing;
    timing.guiApplied = DesktopSceneEngine::Player::TimelinePreviewTiming::Clock::now();
    lastTimelineHoverPreviewTiming_ = timing;
    lastTimelineHoverPreviewStoreOutcome_ = result.storeOutcome;

    if (result.image.isNull())
    {
        setTimelineHoverPreviewUnavailable();
        return;
    }

    refreshTimelineHoverPreviewPresentation();
    const DesktopSceneEngine::Domain::Scene* const activeScene =
        activeTimelinePreviewScene();
    if (activeScene == nullptr)
    {
        setTimelineHoverPreviewUnavailable();
        return;
    }

    const QImage composed = DesktopSceneEngine::Player::composeTimelineCanvasPreview(
        result.image,
        *activeScene,
        timelineHoverPreviewCanvasSize_);
    if (composed.isNull())
    {
        setTimelineHoverPreviewUnavailable();
        return;
    }

    displayedTimelineHoverPreview_ = DisplayedTimelineHoverPreview{
        result.image,
        {timelinePreviewIdentity(
            result.request.source,
            result.request.timeSeconds,
            result.request.generation,
            result.request.serial,
            result.request.representativeTimestampMilliseconds),
         timelineHoverPreviewPresentationGeneration_}};
    timelineHoverPopup_->setImage(composed);
}

QSize MainWindow::timelineHoverPopupCanvasSize() const noexcept
{
    return DesktopSceneEngine::Player::timelinePreviewPopupCanvasSize(
        workspaceModel_.desktopCanvas().aspectRatio);
}

void MainWindow::refreshTimelineHoverPreviewPresentation(bool force)
{
    const QSize canvasSize = timelineHoverPopupCanvasSize();
    if (canvasSize.isEmpty())
    {
        timelinePreviewCompositionDirty_ = true;
        return;
    }

    const bool canvasChanged = canvasSize != timelineHoverPreviewCanvasSize_;
    if (!force && !canvasChanged && !timelinePreviewCompositionDirty_)
    {
        return;
    }

    timelineHoverPreviewCanvasSize_ = canvasSize;
    ++timelineHoverPreviewPresentationGeneration_;
    if (timelineHoverPopup_ != nullptr)
    {
        timelineHoverPopup_->setCanvasSize(canvasSize);
    }

    if (!displayedTimelineHoverPreview_.has_value())
    {
        timelinePreviewCompositionDirty_ = false;
        updateTimelineHoverPopupGeometry();
        return;
    }

    if (!timelineHoverActive_)
    {
        timelinePreviewCompositionDirty_ = true;
        updateTimelineHoverPopupGeometry();
        return;
    }

    const DesktopSceneEngine::Domain::Scene* const activeScene =
        activeTimelinePreviewScene();
    if (activeScene == nullptr)
    {
        setTimelineHoverPreviewUnavailable();
        return;
    }

    const QImage composed = DesktopSceneEngine::Player::composeTimelineCanvasPreview(
        displayedTimelineHoverPreview_->sourceFrame,
        *activeScene,
        canvasSize);
    if (composed.isNull())
    {
        setTimelineHoverPreviewUnavailable();
        return;
    }

    displayedTimelineHoverPreview_->identity.presentationGeneration =
        timelineHoverPreviewPresentationGeneration_;
    timelinePreviewCompositionDirty_ = false;
    if (timelineHoverPopup_ != nullptr)
    {
        timelineHoverPopup_->setImage(composed);
    }

    updateTimelineHoverPopupGeometry();
}

void MainWindow::setTimelineHoverPreviewPending()
{
    if (timelineHoverPopup_ != nullptr)
    {
        timelineHoverPopup_->setPending();
    }
}

void MainWindow::setTimelineHoverPreviewUnavailable()
{
    displayedTimelineHoverPreview_.reset();
    if (timelineHoverPopup_ != nullptr)
    {
        timelineHoverPopup_->setUnavailable(tr("Preview unavailable"));
    }
}

void MainWindow::updateTimelineHoverPopupGeometry()
{
    QWidget* const central = centralWidget();
    if (timelineHoverPopup_ == nullptr || !timelineHoverPopup_->isVisible()
        || !timelineHoverPoint_.has_value() || central == nullptr
        || positionSlider_ == nullptr || previewViewportHost_ == nullptr)
    {
        return;
    }

    const QRect previewBounds{
        previewViewportHost_->mapTo(central, QPoint{}),
        previewViewportHost_->size()};
    if (previewBounds.isEmpty())
    {
        timelineHoverPopup_->hide();
        return;
    }

    const QPoint hoverPointInCentral = positionSlider_->mapTo(
        central,
        *timelineHoverPoint_);
    const int popupX = DesktopSceneEngine::Player::clampedTimelineHoverPopupX(
        hoverPointInCentral.x(),
        timelineHoverPopup_->width(),
        previewBounds);
    const int timelineTop = positionSlider_->mapTo(central, QPoint{}).y();
    const int popupY = timelineTop - timelineHoverPopup_->height() - 10;
    timelineHoverPopup_->move(popupX, popupY);
}

void MainWindow::hideTimelineHoverPopup() noexcept
{
    timelineHoverPreviewTimer_.stop();
    pendingTimelineHoverPreview_.reset();
    timelinePreviewRequestState_.invalidatePending();
    ++timelineHoverPreviewRequestSerial_;
    timelineHoverActive_ = false;
    timelineHoverPoint_.reset();
    timelineHoverRawTimeSeconds_.reset();
    if (timelineHoverPopup_ != nullptr)
    {
        timelineHoverPopup_->hide();
    }
}

void MainWindow::invalidateTimelineHoverPreview() noexcept
{
    hideTimelineHoverPopup();
    displayedTimelineHoverPreview_.reset();
    timelinePreviewProvider_.clear();
    clearTimelinePreviewWorkerSource();
    timelineHoverPreviewSource_.reset();
    ++timelineHoverPreviewGeneration_;
}

void MainWindow::clearTimelinePreviewWorkerSource() noexcept
{
    if (timelinePreviewWorker_ != nullptr && timelinePreviewWorkerThread_.isRunning())
    {
        (void)QMetaObject::invokeMethod(
            timelinePreviewWorker_,
            &DesktopSceneEngine::Player::TimelinePreviewWorker::clearSource,
            Qt::QueuedConnection);
    }
}

void MainWindow::reconcileTimelinePreviewPreloadTarget()
{
    const auto source = activeTimelinePreviewSource();
    const auto durationMillisecondsSnapshot = durationMilliseconds(engine_.effectiveDuration());
    if (!source.has_value() || source->mediaType != WorkspaceMediaType::Video
        || !durationMillisecondsSnapshot.has_value())
    {
        invalidateTimelinePreviewPreloadTarget();
        return;
    }

    if (timelinePreviewPreloadSource_.has_value()
        && *timelinePreviewPreloadSource_ == *source
        && timelinePreviewPreloadDurationMilliseconds_ == durationMillisecondsSnapshot)
    {
        return;
    }

    ++timelinePreviewPreloadGeneration_;
    timelinePreviewPreloadSource_ = *source;
    timelinePreviewPreloadDurationMilliseconds_ = *durationMillisecondsSnapshot;
    timelinePreviewRequestState_.fencePreloadGeneration(timelinePreviewPreloadGeneration_);

    if (timelinePreviewWorker_ != nullptr && timelinePreviewWorkerThread_.isRunning())
    {
        auto* const worker = timelinePreviewWorker_;
        const TimelinePreviewSource sourceCopy = *source;
        const std::int64_t durationMillisecondsCopy = *durationMillisecondsSnapshot;
        const std::uint64_t generation = timelinePreviewPreloadGeneration_;
        (void)QMetaObject::invokeMethod(
            worker,
            [worker, sourceCopy, durationMillisecondsCopy, generation]
            {
                worker->reconcilePreloadTarget(
                    sourceCopy,
                    durationMillisecondsCopy,
                    generation);
            },
            Qt::QueuedConnection);
    }
}

void MainWindow::invalidateTimelinePreviewPreloadTarget() noexcept
{
    if (!timelinePreviewPreloadSource_.has_value()
        && !timelinePreviewPreloadDurationMilliseconds_.has_value())
    {
        return;
    }

    ++timelinePreviewPreloadGeneration_;
    timelinePreviewPreloadSource_.reset();
    timelinePreviewPreloadDurationMilliseconds_.reset();
    timelinePreviewRequestState_.fencePreloadGeneration(timelinePreviewPreloadGeneration_);

    if (timelinePreviewWorker_ != nullptr && timelinePreviewWorkerThread_.isRunning())
    {
        auto* const worker = timelinePreviewWorker_;
        const std::uint64_t generation = timelinePreviewPreloadGeneration_;
        (void)QMetaObject::invokeMethod(
            worker,
            [worker, generation]
            {
                worker->invalidatePreload(generation);
            },
            Qt::QueuedConnection);
    }
}

bool MainWindow::timelineDragTargetsCurrentScene(float duration) const noexcept
{
    return timelineDragging_
        && timelineDragItemId_.has_value()
        && activeTimelineItemId() == timelineDragItemId_
        && DesktopSceneEngine::Player::hasValidTimelineDuration(duration)
        && std::fabs(duration - timelineDragDuration_) <= 0.0001F;
}

void MainWindow::beginTimelineDrag()
{
    hideTimelineHoverPopup();
    if (positionSlider_ == nullptr || !positionSlider_->isEnabled())
    {
        return;
    }

    const float duration = engine_.effectiveDuration();
    const auto itemId = activeTimelineItemId();
    if (!DesktopSceneEngine::Player::hasValidTimelineDuration(duration)
        || !itemId.has_value())
    {
        cancelTimelineDrag();
        return;
    }

    timelineDragging_ = true;
    timelineDragItemId_ = itemId;
    timelineDragDuration_ = duration;
}

void MainWindow::previewTimelinePosition(int sliderValue)
{
    if (!timelineDragging_)
    {
        return;
    }

    if (!timelineDragTargetsCurrentScene(engine_.effectiveDuration()))
    {
        cancelTimelineDrag();
        refreshStatus();
        return;
    }

    if (transportTimeLabel_ != nullptr)
    {
        const float previewTime = DesktopSceneEngine::Player::timelineTimeForValue(
            sliderValue,
            timelineDragDuration_);
        transportTimeLabel_->setText(QString::fromStdString(
            DesktopSceneEngine::Player::formatTimelineTime(previewTime)
            + " / "
            + DesktopSceneEngine::Player::formatTimelineTime(timelineDragDuration_)));
    }
}

void MainWindow::commitTimelineDrag()
{
    if (!timelineDragging_)
    {
        return;
    }

    const int sliderValue = positionSlider_ != nullptr ? positionSlider_->value() : 0;
    const float duration = engine_.effectiveDuration();
    const bool commit = timelineDragTargetsCurrentScene(duration);
    const float targetTime = DesktopSceneEngine::Player::timelineTimeForValue(
        sliderValue,
        timelineDragDuration_);
    cancelTimelineDrag();

    if (!commit)
    {
        refreshStatus();
        return;
    }

    if (DesktopSceneEngine::Player::isTimelineEndIntent(sliderValue))
    {
        navigateScene(false);
        return;
    }

    (void)engine_.seek(targetTime);
    refreshStatus();
}

void MainWindow::cancelTimelineDrag() noexcept
{
    timelineDragging_ = false;
    timelineDragItemId_.reset();
    timelineDragDuration_ = 0.0F;
}

void MainWindow::setPlaybackButtonText()
{
    if (playPauseButton_ != nullptr)
    {
        playPauseButton_->setText(
            engine_.sceneCount() == 0U || engine_.isPaused()
                ? tr("Play")
                : tr("Pause"));
    }
}

void MainWindow::updateEffectiveOutputVolume()
{
    if (!engineInitialized_)
    {
        return;
    }

    engine_.setVolume(static_cast<float>(
        DesktopSceneEngine::Player::calculateEffectiveVolume(
            static_cast<double>(activePlaylistMasterVolume()),
            activeItemMediaVolume())));
}

void MainWindow::synchronizeActivePlaybackVolumeCluster()
{
    const bool hasActivePlayback = activePlaybackProjection_.has_value();
    if (volumeCluster_ != nullptr)
    {
        volumeCluster_->setVisible(hasActivePlayback);
    }

    if (activePlaylistNameLabel_ != nullptr && !hasActivePlayback)
    {
        activePlaylistNameLabel_->clear();
    }

    if (volumeSlider_ == nullptr)
    {
        return;
    }

    const int percentage = static_cast<int>(std::lround(
        currentPlaylistMasterVolume() * 100.0F));
    const QSignalBlocker blocker{volumeSlider_};
    volumeSlider_->setValue(percentage);
    updateActivePlaylistNameElision();
}

void MainWindow::updateActivePlaylistNameElision()
{
    if (activePlaylistNameLabel_ == nullptr)
    {
        return;
    }

    QString displayAlias{};
    if (activePlaybackProjection_.has_value())
    {
        const auto* playlist = workspaceModel_.playlist(
            activePlaybackProjection_->sourcePlaylistId);
        if (playlist != nullptr)
        {
            displayAlias = playlist->displayAlias;
        }
    }

    const int availableWidth = activePlaylistNameLabel_->contentsRect().width();
    const QString rendered = availableWidth > 0
        ? QFontMetrics{activePlaylistNameLabel_->font()}.elidedText(
            displayAlias,
            Qt::ElideRight,
            availableWidth)
        : QString{};
    if (activePlaylistNameLabel_->text() != rendered)
    {
        activePlaylistNameLabel_->setText(rendered);
    }
}

void MainWindow::synchronizePlaylistInspectorMasterVolume(PlaylistId id)
{
    if (inspectorPlaylistMasterVolumeSlider_ == nullptr
        || inspectorPlaylistMasterVolumeValueLabel_ == nullptr
        || inspectorPlaylistMasterVolumePlaylistId_ != id)
    {
        return;
    }

    const auto* playlist = workspaceModel_.playlist(id);
    if (playlist == nullptr)
    {
        return;
    }

    const int percentage = static_cast<int>(std::lround(
        std::clamp(static_cast<float>(playlist->masterVolume), 0.0F, 1.0F) * 100.0F));
    const QSignalBlocker blocker{inspectorPlaylistMasterVolumeSlider_};
    inspectorPlaylistMasterVolumeSlider_->setValue(percentage);
    inspectorPlaylistMasterVolumeValueLabel_->setText(QStringLiteral("%1%").arg(percentage));
}

PlaylistId MainWindow::volumeControlPlaylistId() const noexcept
{
    return activePlaybackProjection_.has_value()
        ? activePlaybackProjection_->sourcePlaylistId
        : PlaylistId{};
}

float MainWindow::currentPlaylistMasterVolume() const noexcept
{
    const auto* playlist = workspaceModel_.playlist(volumeControlPlaylistId());
    if (playlist == nullptr)
    {
        return 1.0F;
    }

    return std::clamp(static_cast<float>(playlist->masterVolume), 0.0F, 1.0F);
}

float MainWindow::activePlaylistMasterVolume() const noexcept
{
    if (!activePlaybackProjection_.has_value())
    {
        return 1.0F;
    }

    const auto* playlist = workspaceModel_.playlist(
        activePlaybackProjection_->sourcePlaylistId);
    if (playlist == nullptr)
    {
        return 1.0F;
    }

    return std::clamp(static_cast<float>(playlist->masterVolume), 0.0F, 1.0F);
}

double MainWindow::activeItemMediaVolume() const noexcept
{
    const auto itemId = activeTimelineItemId();
    if (!itemId.has_value())
    {
        return 1.0;
    }

    const auto* item = workspaceModel_.item(*itemId);
    if (item == nullptr || item->mediaType != WorkspaceMediaType::Video
        || !std::isfinite(item->itemMediaVolume))
    {
        return 1.0;
    }

    return std::clamp(item->itemMediaVolume, 0.0, 1.0);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    closeAfterDecision(event, maybeSaveCurrentDocument());
}

void MainWindow::closeAfterDecision(
    QCloseEvent* event,
    DestructiveDecision decision)
{
    if (event == nullptr)
    {
        return;
    }

    if (decision == DestructiveDecision::Abort)
    {
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
    if (event->isAccepted())
    {
        trayPresentation_.beginTeardown();
        if (acceptedCloseCallback_) acceptedCloseCallback_();
        shutdownEngine();
        closeAllBackgroundMusicWindows();
    }
}
