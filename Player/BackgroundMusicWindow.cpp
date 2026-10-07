#include "BackgroundMusicWindow.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace DesktopSceneEngine::Player
{

namespace
{

constexpr int identityRole = Qt::UserRole;
constexpr int baseAliasRole = Qt::UserRole + 1;

[[nodiscard]] QString uiText(const char* sourceText)
{
    return QCoreApplication::translate("BackgroundMusicWindow", sourceText);
}

[[nodiscard]] QLabel* sectionHeader(QWidget* parent, const QString& title)
{
    auto* label = new QLabel(title, parent);
    label->setStyleSheet(
        QStringLiteral(
            "QLabel { color: #9aa6b2; font-size: 11px; "
            "font-weight: 600; letter-spacing: 1px; }"));
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

void makeHorizontallyShrinkable(QWidget* widget)
{
    widget->setMinimumWidth(0);
    widget->setSizePolicy(QSizePolicy::Ignored, widget->sizePolicy().verticalPolicy());
}

[[nodiscard]] TrackId trackIdFor(const QTreeWidgetItem* item)
{
    return item == nullptr ? TrackId{} : TrackId{item->data(0, identityRole).toUuid()};
}

[[nodiscard]] ItemId itemIdFor(const QListWidgetItem* item)
{
    return item == nullptr ? ItemId{} : ItemId{item->data(identityRole).toUuid()};
}

[[nodiscard]] std::optional<MusicPlaybackMode> musicPlaybackModeFor(
    const QComboBox* comboBox)
{
    if (comboBox == nullptr || comboBox->currentIndex() < 0)
    {
        return std::nullopt;
    }

    switch (comboBox->currentData().toInt())
    {
    case static_cast<int>(MusicPlaybackMode::CurrentSingleLoop):
        return MusicPlaybackMode::CurrentSingleLoop;
    case static_cast<int>(MusicPlaybackMode::ListLoop):
        return MusicPlaybackMode::ListLoop;
    case static_cast<int>(MusicPlaybackMode::Shuffle):
        return MusicPlaybackMode::Shuffle;
    default:
        return std::nullopt;
    }
}

void setMusicPlaybackMode(QComboBox* comboBox, MusicPlaybackMode mode)
{
    const QSignalBlocker blocker{comboBox};
    comboBox->setCurrentIndex(comboBox->findData(static_cast<int>(mode)));
}

void setVolume(QSlider* slider, QLabel* label, double volume)
{
    const int percentage = std::clamp(
        static_cast<int>(std::lround(volume * 100.0)),
        0,
        100);
    const QSignalBlocker blocker{slider};
    slider->setValue(percentage);
    label->setText(QStringLiteral("%1%").arg(percentage));
}

void configureMusicModeCombo(QComboBox* comboBox)
{
    comboBox->addItem(
        uiText("Current Single Loop"),
        static_cast<int>(MusicPlaybackMode::CurrentSingleLoop));
    comboBox->addItem(
        uiText("List Loop"),
        static_cast<int>(MusicPlaybackMode::ListLoop));
    comboBox->addItem(
        uiText("Shuffle"),
        static_cast<int>(MusicPlaybackMode::Shuffle));
    comboBox->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
}

void retranslateMusicModeCombo(QComboBox* comboBox)
{
    const QSignalBlocker blocker{comboBox};
    comboBox->setItemText(
        comboBox->findData(static_cast<int>(MusicPlaybackMode::CurrentSingleLoop)),
        uiText("Current Single Loop"));
    comboBox->setItemText(
        comboBox->findData(static_cast<int>(MusicPlaybackMode::ListLoop)),
        uiText("List Loop"));
    comboBox->setItemText(
        comboBox->findData(static_cast<int>(MusicPlaybackMode::Shuffle)),
        uiText("Shuffle"));
}

[[nodiscard]] QWidget* createVolumeRow(
    QWidget* parent,
    QSlider*& slider,
    QLabel*& percentageLabel)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(0, 100);
    slider->setSingleStep(1);
    slider->setPageStep(10);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    percentageLabel = new QLabel(row);
    percentageLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    percentageLabel->setMinimumWidth(
        percentageLabel->fontMetrics().horizontalAdvance(QStringLiteral("100%")));
    percentageLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    layout->addWidget(slider, 1);
    layout->addWidget(percentageLabel);
    return row;
}

void configureTrackTree(QTreeWidget* tree)
{
    tree->setHeaderLabels({uiText("Name"), uiText("Source"), uiText("Volume")});
    tree->setRootIsDecorated(false);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setStyleSheet(QStringLiteral(
        "QTreeWidget::item:selected { "
        "background: palette(highlight); "
        "color: palette(highlighted-text); "
        "}"));
    tree->setTextElideMode(Qt::ElideRight);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tree->setMinimumWidth(0);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree->header()->resizeSection(0, 140);
    tree->header()->resizeSection(
        2,
        std::max(
            tree->fontMetrics().horizontalAdvance(uiText("Volume")),
            tree->fontMetrics().horizontalAdvance(QStringLiteral("100%")))
            + 16);
}

[[nodiscard]] std::optional<TrackId> selectedTrackId(const QTreeWidget* tree)
{
    const TrackId id = trackIdFor(tree->currentItem());
    return id.isValid() ? std::optional<TrackId>{id} : std::nullopt;
}

[[nodiscard]] int trackRow(const QTreeWidget* tree)
{
    return tree->indexOfTopLevelItem(tree->currentItem());
}

[[nodiscard]] int trackIndexFor(const MusicQueueConfig& queue, TrackId id)
{
    for (int index = 0; index < queue.tracks.size(); ++index)
    {
        if (queue.tracks.at(index).id == id)
        {
            return index;
        }
    }

    return -1;
}

[[nodiscard]] const MusicTrack* trackFor(
    const MusicQueueConfig* queue,
    const std::optional<TrackId>& trackId)
{
    if (queue == nullptr || !trackId.has_value())
    {
        return nullptr;
    }

    const int index = trackIndexFor(*queue, *trackId);
    return index >= 0 ? &queue->tracks.at(index) : nullptr;
}

[[nodiscard]] QString volumeText(double volume)
{
    const int percentage = std::clamp(
        static_cast<int>(std::lround(volume * 100.0)),
        0,
        100);
    return QStringLiteral("%1%").arg(percentage);
}

void updateTrackVolumeDisplay(QTreeWidget* tree, TrackId trackId, double volume)
{
    for (int row = 0; row < tree->topLevelItemCount(); ++row)
    {
        QTreeWidgetItem* item = tree->topLevelItem(row);
        if (trackIdFor(item) == trackId)
        {
            item->setText(2, volumeText(volume));
            return;
        }
    }
}

void updatePlayingTrackMarker(
    QTreeWidget* tree,
    const std::optional<TrackId>& playingTrackId)
{
    for (int rowIndex = 0; rowIndex < tree->topLevelItemCount(); ++rowIndex)
    {
        QTreeWidgetItem* const row = tree->topLevelItem(rowIndex);
        const bool isPlaying = playingTrackId.has_value()
            && trackIdFor(row) == *playingTrackId;
        const QString baseAlias = row->data(0, baseAliasRole).toString();
        row->setText(
            0,
            isPlaying ? QString::fromUtf8("\xE2\x96\xB6 ") + baseAlias : baseAlias);
    }
}

void setSelectedTrackVolume(
    QSlider* slider,
    QLabel* label,
    const MusicTrack* track)
{
    const bool hasTrack = track != nullptr;
    slider->setEnabled(hasTrack);
    label->setEnabled(hasTrack);
    if (hasTrack)
    {
        setVolume(slider, label, track->volume);
        return;
    }

    const QSignalBlocker blocker{slider};
    slider->setValue(0);
    label->setText(QStringLiteral("—"));
}

[[nodiscard]] QString backgroundMusicFileFilter()
{
    return uiText("Supported Background Music")
            + QStringLiteral(" (*.mp3 *.aac *.m4a *.flac *.wav *.ogg *.opus *.wma *.alac *.aiff *.aif *.ape);;")
            + uiText("Tier 1 Audio")
            + QStringLiteral(" (*.mp3 *.aac *.m4a *.flac *.wav *.ogg *.opus);;")
            + uiText("Tier 2 Audio")
            + QStringLiteral(" (*.wma *.alac *.aiff *.aif *.ape)");
}

}

BackgroundMusicWindow::BackgroundMusicWindow(
    WorkspaceModel& workspaceModel,
    PlaylistId playlistId,
    QWidget* parent)
    : QDialog(parent)
    , workspaceModel_(workspaceModel)
    , playlistId_(playlistId)
{
    setModal(false);
    resize(1080, 700);
    setMinimumSize(840, 560);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    outerSplitter_ = new QSplitter(Qt::Horizontal, this);
    outerSplitter_->setObjectName(QStringLiteral("backgroundMusicOuterSplitter"));
    outerSplitter_->setChildrenCollapsible(false);
    outerSplitter_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    playlistPane_ = new QWidget(outerSplitter_);
    playlistPane_->setMinimumWidth(340);
    playlistPane_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* playlistLayout = new QVBoxLayout(playlistPane_);
    playlistLayout->setContentsMargins(10, 10, 10, 10);
    playlistLayout->setSpacing(6);
    playlistHeading_ = sectionHeader(playlistPane_, uiText("Playlist Global BGM"));
    playlistLayout->addWidget(playlistHeading_);

    auto* playlistSettings = new QFormLayout();
    playlistSettings->setRowWrapPolicy(QFormLayout::WrapLongRows);
    playlistSettings->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    playlistSettings->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    playlistEnabled_ = new QCheckBox(uiText("Enabled"), playlistPane_);
    playlistEnabled_->setObjectName(QStringLiteral("playlistMusicEnabledCheckBox"));
    playlistSettings->addRow(playlistEnabled_);

    playlistMode_ = new QComboBox(playlistPane_);
    playlistMode_->setObjectName(QStringLiteral("playlistMusicModeCombo"));
    configureMusicModeCombo(playlistMode_);
    playlistModeLabel_ = new QLabel(uiText("Playback Mode"), playlistPane_);
    playlistSettings->addRow(playlistModeLabel_, playlistMode_);

    auto* playlistVolumeRow = createVolumeRow(
        playlistPane_,
        playlistVolume_,
        playlistVolumeLabel_);
    playlistVolume_->setObjectName(QStringLiteral("playlistMusicVolumeSlider"));
    playlistVolumeLabel_->setObjectName(QStringLiteral("playlistMusicVolumeLabel"));
    playlistVolumeRowLabel_ = new QLabel(uiText("Queue Volume"), playlistPane_);
    playlistSettings->addRow(playlistVolumeRowLabel_, playlistVolumeRow);
    playlistLayout->addLayout(playlistSettings);
    playlistTracksLabel_ = new QLabel(uiText("Tracks"), playlistPane_);
    playlistLayout->addWidget(playlistTracksLabel_);

    playlistTracks_ = new QTreeWidget(playlistPane_);
    playlistTracks_->setObjectName(QStringLiteral("playlistMusicTracksTree"));
    configureTrackTree(playlistTracks_);
    playlistTracks_->viewport()->installEventFilter(this);
    playlistLayout->addWidget(playlistTracks_, 1);

    auto* playlistTrackVolumeSettings = new QFormLayout();
    playlistTrackVolumeSettings->setRowWrapPolicy(QFormLayout::WrapLongRows);
    playlistTrackVolumeSettings->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    playlistTrackVolumeSettings->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* playlistTrackVolumeRow = createVolumeRow(
        playlistPane_,
        playlistTrackVolume_,
        playlistTrackVolumeLabel_);
    playlistTrackVolume_->setObjectName(QStringLiteral("playlistMusicTrackVolumeSlider"));
    playlistTrackVolumeLabel_->setObjectName(QStringLiteral("playlistMusicTrackVolumeLabel"));
    playlistTrackVolumeRowLabel_ = new QLabel(uiText("Selected Track Volume"), playlistPane_);
    playlistTrackVolumeSettings->addRow(playlistTrackVolumeRowLabel_, playlistTrackVolumeRow);
    playlistLayout->addLayout(playlistTrackVolumeSettings);

    auto* playlistActions = new QHBoxLayout();
    playlistActions->setContentsMargins(0, 0, 0, 0);
    playlistActions->setSpacing(6);
    playlistAddButton_ = new QPushButton(uiText("Add"), playlistPane_);
    playlistRemoveButton_ = new QPushButton(uiText("Remove"), playlistPane_);
    playlistMoveUpButton_ = new QPushButton(uiText("Move Up"), playlistPane_);
    playlistMoveDownButton_ = new QPushButton(uiText("Move Down"), playlistPane_);
    playlistAddButton_->setObjectName(QStringLiteral("playlistMusicAddButton"));
    playlistRemoveButton_->setObjectName(QStringLiteral("playlistMusicRemoveButton"));
    playlistMoveUpButton_->setObjectName(QStringLiteral("playlistMusicMoveUpButton"));
    playlistMoveDownButton_->setObjectName(QStringLiteral("playlistMusicMoveDownButton"));
    playlistActions->addWidget(playlistAddButton_);
    playlistActions->addWidget(playlistRemoveButton_);
    playlistActions->addWidget(playlistMoveUpButton_);
    playlistActions->addWidget(playlistMoveDownButton_);
    playlistLayout->addLayout(playlistActions);

    itemPane_ = new QWidget(outerSplitter_);
    itemPane_->setMinimumWidth(420);
    itemPane_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* itemPaneLayout = new QVBoxLayout(itemPane_);
    itemPaneLayout->setContentsMargins(10, 10, 10, 10);
    itemPaneLayout->setSpacing(6);
    itemHeading_ = sectionHeader(itemPane_, uiText("Item Background Music"));
    itemPaneLayout->addWidget(itemHeading_);

    itemSplitter_ = new QSplitter(Qt::Vertical, itemPane_);
    itemSplitter_->setObjectName(QStringLiteral("backgroundMusicItemSplitter"));
    itemSplitter_->setChildrenCollapsible(false);
    itemSplitter_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* itemSelectorPane = new QWidget(itemSplitter_);
    itemSelectorPane->setMinimumHeight(140);
    auto* itemSelectorLayout = new QVBoxLayout(itemSelectorPane);
    itemSelectorLayout->setContentsMargins(0, 0, 0, 0);
    itemSelectorLayout->setSpacing(6);
    itemListLabel_ = new QLabel(uiText("Items"), itemSelectorPane);
    itemSelectorLayout->addWidget(itemListLabel_);
    itemList_ = new QListWidget(itemSelectorPane);
    itemList_->setObjectName(QStringLiteral("musicItemList"));
    itemList_->setSelectionMode(QAbstractItemView::SingleSelection);
    itemList_->setTextElideMode(Qt::ElideRight);
    itemList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    itemList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    itemList_->setMinimumWidth(0);
    itemSelectorLayout->addWidget(itemList_, 1);

    itemEditorPane_ = new QWidget(itemSplitter_);
    itemEditorPane_->setObjectName(QStringLiteral("itemMusicEditorPane"));
    itemEditorPane_->setMinimumHeight(270);
    itemEditorPane_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* itemEditorLayout = new QVBoxLayout(itemEditorPane_);
    itemEditorLayout->setContentsMargins(0, 0, 0, 0);
    itemEditorLayout->setSpacing(6);
    selectedItemHeading_ = new QLabel(itemEditorPane_);
    selectedItemHeading_->setObjectName(QStringLiteral("selectedMusicItemHeading"));
    makeHorizontallyShrinkable(selectedItemHeading_);
    itemEditorLayout->addWidget(selectedItemHeading_);

    auto* itemSettings = new QFormLayout();
    itemSettings->setRowWrapPolicy(QFormLayout::WrapLongRows);
    itemSettings->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    itemSettings->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    itemEnabled_ = new QCheckBox(uiText("Enabled"), itemEditorPane_);
    itemEnabled_->setObjectName(QStringLiteral("itemMusicEnabledCheckBox"));
    itemSettings->addRow(itemEnabled_);

    itemMode_ = new QComboBox(itemEditorPane_);
    itemMode_->setObjectName(QStringLiteral("itemMusicModeCombo"));
    configureMusicModeCombo(itemMode_);
    itemModeLabel_ = new QLabel(uiText("Playback Mode"), itemEditorPane_);
    itemSettings->addRow(itemModeLabel_, itemMode_);

    auto* itemVolumeRow = createVolumeRow(itemEditorPane_, itemVolume_, itemVolumeLabel_);
    itemVolume_->setObjectName(QStringLiteral("itemMusicVolumeSlider"));
    itemVolumeLabel_->setObjectName(QStringLiteral("itemMusicVolumeLabel"));
    itemVolumeRowLabel_ = new QLabel(uiText("Queue Volume"), itemEditorPane_);
    itemSettings->addRow(itemVolumeRowLabel_, itemVolumeRow);
    itemEditorLayout->addLayout(itemSettings);
    itemTracksLabel_ = new QLabel(uiText("Tracks"), itemEditorPane_);
    itemEditorLayout->addWidget(itemTracksLabel_);

    itemTracks_ = new QTreeWidget(itemEditorPane_);
    itemTracks_->setObjectName(QStringLiteral("itemMusicTracksTree"));
    configureTrackTree(itemTracks_);
    itemTracks_->viewport()->installEventFilter(this);
    itemEditorLayout->addWidget(itemTracks_, 1);

    auto* itemTrackVolumeSettings = new QFormLayout();
    itemTrackVolumeSettings->setRowWrapPolicy(QFormLayout::WrapLongRows);
    itemTrackVolumeSettings->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    itemTrackVolumeSettings->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* itemTrackVolumeRow = createVolumeRow(
        itemEditorPane_,
        itemTrackVolume_,
        itemTrackVolumeLabel_);
    itemTrackVolume_->setObjectName(QStringLiteral("itemMusicTrackVolumeSlider"));
    itemTrackVolumeLabel_->setObjectName(QStringLiteral("itemMusicTrackVolumeLabel"));
    itemTrackVolumeRowLabel_ = new QLabel(uiText("Selected Track Volume"), itemEditorPane_);
    itemTrackVolumeSettings->addRow(itemTrackVolumeRowLabel_, itemTrackVolumeRow);
    itemEditorLayout->addLayout(itemTrackVolumeSettings);

    auto* itemActions = new QHBoxLayout();
    itemActions->setContentsMargins(0, 0, 0, 0);
    itemActions->setSpacing(6);
    itemAddButton_ = new QPushButton(uiText("Add"), itemEditorPane_);
    itemRemoveButton_ = new QPushButton(uiText("Remove"), itemEditorPane_);
    itemMoveUpButton_ = new QPushButton(uiText("Move Up"), itemEditorPane_);
    itemMoveDownButton_ = new QPushButton(uiText("Move Down"), itemEditorPane_);
    itemAddButton_->setObjectName(QStringLiteral("itemMusicAddButton"));
    itemRemoveButton_->setObjectName(QStringLiteral("itemMusicRemoveButton"));
    itemMoveUpButton_->setObjectName(QStringLiteral("itemMusicMoveUpButton"));
    itemMoveDownButton_->setObjectName(QStringLiteral("itemMusicMoveDownButton"));
    itemActions->addWidget(itemAddButton_);
    itemActions->addWidget(itemRemoveButton_);
    itemActions->addWidget(itemMoveUpButton_);
    itemActions->addWidget(itemMoveDownButton_);
    itemEditorLayout->addLayout(itemActions);

    itemPaneLayout->addWidget(itemSplitter_, 1);
    outerSplitter_->addWidget(playlistPane_);
    outerSplitter_->addWidget(itemPane_);
    outerSplitter_->setStretchFactor(0, 45);
    outerSplitter_->setStretchFactor(1, 55);
    outerSplitter_->setSizes({450, 550});
    itemSplitter_->setStretchFactor(0, 35);
    itemSplitter_->setStretchFactor(1, 65);
    itemSplitter_->setSizes({210, 390});
    rootLayout->addWidget(outerSplitter_, 1);

    connect(
        playlistTracks_,
        &QTreeWidget::itemSelectionChanged,
        this,
        [this]()
        {
            updatePlaylistActionButtons();
        });
    connect(
        itemTracks_,
        &QTreeWidget::itemSelectionChanged,
        this,
        [this]()
        {
            updateItemActionButtons();
        });
    connect(
        itemList_,
        &QListWidget::currentItemChanged,
        this,
        [this](QListWidgetItem* current)
        {
            const ItemId id = itemIdFor(current);
            selectedItemId_ = id.isValid() ? std::optional<ItemId>{id} : std::nullopt;
            refreshSelectedItemMusic();
        });
    connect(
        playlistEnabled_,
        &QCheckBox::toggled,
        this,
        [this](bool enabled)
        {
            if (playlistEnabledChangedCallback_)
            {
                playlistEnabledChangedCallback_(playlistId_, enabled);
            }
        });
    connect(
        itemEnabled_,
        &QCheckBox::toggled,
        this,
        [this](bool enabled)
        {
            if (selectedItemId_.has_value() && itemEnabledChangedCallback_)
            {
                itemEnabledChangedCallback_(playlistId_, *selectedItemId_, enabled);
            }
        });
    connect(
        playlistMode_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int)
        {
            const auto mode = musicPlaybackModeFor(playlistMode_);
            if (mode.has_value()
                && workspaceModel_.setPlaylistMusicPlaybackMode(playlistId_, *mode))
            {
                notifyQueueChanged(std::nullopt);
            }
        });
    connect(
        itemMode_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int)
        {
            if (!selectedItemId_.has_value())
            {
                return;
            }

            const auto mode = musicPlaybackModeFor(itemMode_);
            if (mode.has_value()
                && workspaceModel_.setItemMusicPlaybackMode(*selectedItemId_, *mode))
            {
                notifyQueueChanged(*selectedItemId_);
            }
        });
    connect(
        playlistVolume_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            if (workspaceModel_.setPlaylistMusicVolume(
                    playlistId_,
                    static_cast<double>(value) / 100.0))
            {
                playlistVolumeLabel_->setText(QStringLiteral("%1%").arg(value));
                notifyQueueChanged(std::nullopt);
            }
        });
    connect(
        itemVolume_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            if (selectedItemId_.has_value()
                && workspaceModel_.setItemMusicVolume(
                    *selectedItemId_,
                    static_cast<double>(value) / 100.0))
            {
                itemVolumeLabel_->setText(QStringLiteral("%1%").arg(value));
                notifyQueueChanged(*selectedItemId_);
            }
        });
    connect(
        playlistTrackVolume_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            const auto trackId = selectedTrackId(playlistTracks_);
            if (trackId.has_value()
                && workspaceModel_.setPlaylistMusicTrackVolume(
                    playlistId_,
                    *trackId,
                    static_cast<double>(value) / 100.0))
            {
                updateTrackVolumeDisplay(
                    playlistTracks_,
                    *trackId,
                    static_cast<double>(value) / 100.0);
                updatePlaylistSelectedTrackVolume();
                notifyQueueChanged(std::nullopt);
            }
        });
    connect(
        itemTrackVolume_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            const auto trackId = selectedTrackId(itemTracks_);
            if (selectedItemId_.has_value() && trackId.has_value()
                && workspaceModel_.setItemMusicTrackVolume(
                    *selectedItemId_,
                    *trackId,
                    static_cast<double>(value) / 100.0))
            {
                updateTrackVolumeDisplay(
                    itemTracks_,
                    *trackId,
                    static_cast<double>(value) / 100.0);
                updateItemSelectedTrackVolume();
                notifyQueueChanged(*selectedItemId_);
            }
        });
    connect(playlistAddButton_, &QPushButton::clicked, this, [this]()
    {
        choosePlaylistMusicTracks();
    });
    connect(playlistRemoveButton_, &QPushButton::clicked, this, [this]()
    {
        removePlaylistMusicTrack();
    });
    connect(playlistMoveUpButton_, &QPushButton::clicked, this, [this]()
    {
        movePlaylistMusicTrack(-1);
    });
    connect(playlistMoveDownButton_, &QPushButton::clicked, this, [this]()
    {
        movePlaylistMusicTrack(1);
    });
    connect(itemAddButton_, &QPushButton::clicked, this, [this]()
    {
        chooseItemMusicTracks();
    });
    connect(itemRemoveButton_, &QPushButton::clicked, this, [this]()
    {
        removeItemMusicTrack();
    });
    connect(itemMoveUpButton_, &QPushButton::clicked, this, [this]()
    {
        moveItemMusicTrack(-1);
    });
    connect(itemMoveDownButton_, &QPushButton::clicked, this, [this]()
    {
        moveItemMusicTrack(1);
    });

    refreshFromModel();
}

void BackgroundMusicWindow::setQueueChangedCallback(QueueChangedCallback callback)
{
    queueChangedCallback_ = std::move(callback);
}

void BackgroundMusicWindow::setPlaylistEnabledChangedCallback(
    PlaylistEnabledChangedCallback callback)
{
    playlistEnabledChangedCallback_ = std::move(callback);
}

void BackgroundMusicWindow::setItemEnabledChangedCallback(ItemEnabledChangedCallback callback)
{
    itemEnabledChangedCallback_ = std::move(callback);
}

void BackgroundMusicWindow::setPlaylistTrackSwitchRequestedCallback(
    PlaylistTrackSwitchRequestedCallback callback)
{
    playlistTrackSwitchRequestedCallback_ = std::move(callback);
}

void BackgroundMusicWindow::setItemTrackSwitchRequestedCallback(
    ItemTrackSwitchRequestedCallback callback)
{
    itemTrackSwitchRequestedCallback_ = std::move(callback);
}

void BackgroundMusicWindow::setPlaybackDisplayState(
    std::optional<PlaylistId> activePlaylistId,
    std::optional<ItemId> activeItemId,
    std::optional<TrackId> playlistTrackId,
    std::optional<TrackId> itemTrackId)
{
    activePlaylistId_ = std::move(activePlaylistId);
    activeItemId_ = std::move(activeItemId);
    playlistTrackId_ = std::move(playlistTrackId);
    itemTrackId_ = std::move(itemTrackId);
    updatePlaybackMarkers();
}

bool BackgroundMusicWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event == nullptr || event->type() != QEvent::MouseButtonDblClick)
    {
        return false;
    }

    const auto* const mouseEvent = static_cast<const QMouseEvent*>(event);
    if (mouseEvent->button() != Qt::LeftButton)
    {
        return false;
    }

    if (watched == playlistTracks_->viewport())
    {
        const TrackId trackId = trackIdFor(
            playlistTracks_->itemAt(mouseEvent->position().toPoint()));
        if (trackId.isValid() && playlistTrackSwitchRequestedCallback_)
        {
            playlistTrackSwitchRequestedCallback_(playlistId_, trackId);
        }
    }
    else if (watched == itemTracks_->viewport())
    {
        const TrackId trackId = trackIdFor(itemTracks_->itemAt(mouseEvent->position().toPoint()));
        if (selectedItemId_.has_value() && trackId.isValid() && itemTrackSwitchRequestedCallback_)
        {
            itemTrackSwitchRequestedCallback_(playlistId_, *selectedItemId_, trackId);
        }
    }

    return false;
}

void BackgroundMusicWindow::refreshFromModel()
{
    retranslateUi();
    const WorkspacePlaylist* playlist = workspaceModel_.playlist(playlistId_);
    if (playlist == nullptr)
    {
        setWindowTitle(uiText("Background Music"));
        selectedItemId_.reset();
        playlistPane_->setEnabled(false);
        itemPane_->setEnabled(false);
        playlistTracks_->clear();
        itemList_->clear();
        itemTracks_->clear();
        updatePlaylistActionButtons();
        updateItemActionButtons();
        return;
    }

    setWindowTitle(uiText("Background Music — %1").arg(playlist->displayAlias));
    playlistPane_->setEnabled(true);
    itemPane_->setEnabled(true);
    refreshPlaylistMusic();
    refreshItemList();
}

void BackgroundMusicWindow::retranslateUi()
{
    playlistHeading_->setText(uiText("Playlist Global BGM"));
    playlistEnabled_->setText(uiText("Enabled"));
    playlistModeLabel_->setText(uiText("Playback Mode"));
    playlistVolumeRowLabel_->setText(uiText("Queue Volume"));
    playlistTracksLabel_->setText(uiText("Tracks"));
    playlistTrackVolumeRowLabel_->setText(uiText("Selected Track Volume"));
    itemHeading_->setText(uiText("Item Background Music"));
    itemEnabled_->setText(uiText("Enabled"));
    itemListLabel_->setText(uiText("Items"));
    itemModeLabel_->setText(uiText("Playback Mode"));
    itemVolumeRowLabel_->setText(uiText("Queue Volume"));
    itemTracksLabel_->setText(uiText("Tracks"));
    itemTrackVolumeRowLabel_->setText(uiText("Selected Track Volume"));
    playlistAddButton_->setText(uiText("Add"));
    playlistRemoveButton_->setText(uiText("Remove"));
    playlistMoveUpButton_->setText(uiText("Move Up"));
    playlistMoveDownButton_->setText(uiText("Move Down"));
    itemAddButton_->setText(uiText("Add"));
    itemRemoveButton_->setText(uiText("Remove"));
    itemMoveUpButton_->setText(uiText("Move Up"));
    itemMoveDownButton_->setText(uiText("Move Down"));
    retranslateMusicModeCombo(playlistMode_);
    retranslateMusicModeCombo(itemMode_);
    playlistTracks_->setHeaderLabels({uiText("Name"), uiText("Source"), uiText("Volume")});
    itemTracks_->setHeaderLabels({uiText("Name"), uiText("Source"), uiText("Volume")});
}

void BackgroundMusicWindow::refreshPlaylistMusic(
    std::optional<TrackId> preferredTrackId,
    int fallbackRow)
{
    const MusicQueueConfig* queue = workspaceModel_.playlistMusicQueue(playlistId_);
    if (queue == nullptr)
    {
        playlistTracks_->clear();
        updatePlaylistActionButtons();
        return;
    }

    if (!preferredTrackId.has_value())
    {
        preferredTrackId = selectedTrackId(playlistTracks_);
    }
    if (fallbackRow < 0)
    {
        fallbackRow = trackRow(playlistTracks_);
    }

    {
        const QSignalBlocker blocker{playlistTracks_};
        playlistTracks_->clear();
        for (const MusicTrack& track : queue->tracks)
        {
            auto* row = new QTreeWidgetItem(
                playlistTracks_,
                {track.displayAlias, track.sourcePath, volumeText(track.volume)});
            row->setData(0, identityRole, track.id.value);
            row->setData(0, baseAliasRole, track.displayAlias);
            row->setToolTip(0, track.displayAlias);
            row->setToolTip(1, track.sourcePath);
        }

        setMusicPlaybackMode(playlistMode_, queue->playbackMode);
        setVolume(playlistVolume_, playlistVolumeLabel_, queue->volume);
        const QSignalBlocker enabledBlocker{playlistEnabled_};
        playlistEnabled_->setChecked(queue->enabled);

        int rowToSelect = -1;
        if (preferredTrackId.has_value())
        {
            rowToSelect = trackIndexFor(*queue, *preferredTrackId);
        }
        if (rowToSelect < 0 && fallbackRow >= 0 && !queue->tracks.isEmpty())
        {
            rowToSelect = std::min(
                fallbackRow,
                static_cast<int>(queue->tracks.size()) - 1);
        }
        if (rowToSelect >= 0)
        {
            playlistTracks_->setCurrentItem(playlistTracks_->topLevelItem(rowToSelect));
        }
    }

    updatePlaylistActionButtons();
    updatePlaylistPlaybackMarker();
}

void BackgroundMusicWindow::refreshItemList()
{
    const WorkspacePlaylist* playlist = workspaceModel_.playlist(playlistId_);
    if (playlist == nullptr)
    {
        selectedItemId_.reset();
        itemList_->clear();
        refreshSelectedItemMusic();
        return;
    }

    const int previousRow = itemList_->currentRow();
    int rowToSelect = -1;
    {
        const QSignalBlocker blocker{itemList_};
        itemList_->clear();
        for (int index = 0; index < playlist->items.size(); ++index)
        {
            const WorkspaceItem& item = playlist->items.at(index);
            auto* row = new QListWidgetItem(item.displayAlias, itemList_);
            row->setData(identityRole, item.id.value);
            row->setToolTip(item.displayAlias);
            if (selectedItemId_.has_value() && item.id == *selectedItemId_)
            {
                rowToSelect = index;
            }
        }

        if (rowToSelect < 0 && !playlist->items.isEmpty())
        {
            rowToSelect = previousRow >= 0
                ? std::min(
                    previousRow,
                    static_cast<int>(playlist->items.size()) - 1)
                : 0;
        }

        if (rowToSelect >= 0)
        {
            itemList_->setCurrentRow(rowToSelect);
            selectedItemId_ = ItemId{itemList_->currentItem()->data(identityRole).toUuid()};
        }
        else
        {
            selectedItemId_.reset();
        }
    }

    refreshSelectedItemMusic();
}

void BackgroundMusicWindow::refreshSelectedItemMusic(
    std::optional<TrackId> preferredTrackId,
    int fallbackRow)
{
    if (!selectedItemBelongsToPlaylist())
    {
        selectedItemId_.reset();
        itemEditorPane_->setEnabled(false);
        selectedItemHeading_->setText(uiText("No item selected"));
        itemTracks_->clear();
        updateItemActionButtons();
        return;
    }

    const WorkspaceItem* item = workspaceModel_.item(*selectedItemId_);
    const MusicQueueConfig* queue = workspaceModel_.itemMusicQueue(*selectedItemId_);
    if (item == nullptr || queue == nullptr)
    {
        selectedItemId_.reset();
        refreshSelectedItemMusic();
        return;
    }

    itemEditorPane_->setEnabled(true);
    selectedItemHeading_->setText(uiText("Selected Item: %1").arg(item->displayAlias));
    if (!preferredTrackId.has_value())
    {
        preferredTrackId = selectedTrackId(itemTracks_);
    }
    if (fallbackRow < 0)
    {
        fallbackRow = trackRow(itemTracks_);
    }

    {
        const QSignalBlocker blocker{itemTracks_};
        itemTracks_->clear();
        for (const MusicTrack& track : queue->tracks)
        {
            auto* row = new QTreeWidgetItem(itemTracks_, {track.displayAlias, track.sourcePath});
            row->setText(2, volumeText(track.volume));
            row->setData(0, identityRole, track.id.value);
            row->setData(0, baseAliasRole, track.displayAlias);
            row->setToolTip(0, track.displayAlias);
            row->setToolTip(1, track.sourcePath);
        }

        setMusicPlaybackMode(itemMode_, queue->playbackMode);
        setVolume(itemVolume_, itemVolumeLabel_, queue->volume);
        const QSignalBlocker enabledBlocker{itemEnabled_};
        itemEnabled_->setChecked(queue->enabled);

        int rowToSelect = -1;
        if (preferredTrackId.has_value())
        {
            rowToSelect = trackIndexFor(*queue, *preferredTrackId);
        }
        if (rowToSelect < 0 && fallbackRow >= 0 && !queue->tracks.isEmpty())
        {
            rowToSelect = std::min(
                fallbackRow,
                static_cast<int>(queue->tracks.size()) - 1);
        }
        if (rowToSelect >= 0)
        {
            itemTracks_->setCurrentItem(itemTracks_->topLevelItem(rowToSelect));
        }
    }

    updateItemActionButtons();
    updateItemPlaybackMarker();
}

void BackgroundMusicWindow::updatePlaylistActionButtons()
{
    const MusicQueueConfig* queue = workspaceModel_.playlistMusicQueue(playlistId_);
    const auto trackId = selectedTrackId(playlistTracks_);
    const int index = queue != nullptr && trackId.has_value()
        ? trackIndexFor(*queue, *trackId)
        : -1;
    playlistAddButton_->setEnabled(queue != nullptr);
    playlistRemoveButton_->setEnabled(index >= 0);
    playlistMoveUpButton_->setEnabled(index > 0);
    playlistMoveDownButton_->setEnabled(queue != nullptr && index >= 0
        && index + 1 < queue->tracks.size());
    updatePlaylistSelectedTrackVolume();
}

void BackgroundMusicWindow::updateItemActionButtons()
{
    const MusicQueueConfig* queue = selectedItemId_.has_value()
        ? workspaceModel_.itemMusicQueue(*selectedItemId_)
        : nullptr;
    const auto trackId = selectedTrackId(itemTracks_);
    const int index = queue != nullptr && trackId.has_value()
        ? trackIndexFor(*queue, *trackId)
        : -1;
    const bool validItem = selectedItemBelongsToPlaylist() && queue != nullptr;
    itemAddButton_->setEnabled(validItem);
    itemRemoveButton_->setEnabled(validItem && index >= 0);
    itemMoveUpButton_->setEnabled(validItem && index > 0);
    itemMoveDownButton_->setEnabled(validItem && index >= 0
        && index + 1 < queue->tracks.size());
    updateItemSelectedTrackVolume();
}

void BackgroundMusicWindow::updatePlaylistSelectedTrackVolume()
{
    setSelectedTrackVolume(
        playlistTrackVolume_,
        playlistTrackVolumeLabel_,
        trackFor(workspaceModel_.playlistMusicQueue(playlistId_), selectedTrackId(playlistTracks_)));
}

void BackgroundMusicWindow::updateItemSelectedTrackVolume()
{
    const MusicQueueConfig* queue = selectedItemId_.has_value()
        ? workspaceModel_.itemMusicQueue(*selectedItemId_)
        : nullptr;
    setSelectedTrackVolume(
        itemTrackVolume_,
        itemTrackVolumeLabel_,
        trackFor(queue, selectedTrackId(itemTracks_)));
}

void BackgroundMusicWindow::updatePlaybackMarkers()
{
    updatePlaylistPlaybackMarker();
    updateItemPlaybackMarker();
}

void BackgroundMusicWindow::updatePlaylistPlaybackMarker()
{
    const auto trackId = activePlaylistId_ == std::optional<PlaylistId>{playlistId_}
        ? playlistTrackId_
        : std::nullopt;
    updatePlayingTrackMarker(playlistTracks_, trackId);
}

void BackgroundMusicWindow::updateItemPlaybackMarker()
{
    const auto trackId = activePlaylistId_ == std::optional<PlaylistId>{playlistId_}
            && selectedItemId_ == activeItemId_
        ? itemTrackId_
        : std::nullopt;
    updatePlayingTrackMarker(itemTracks_, trackId);
}

void BackgroundMusicWindow::choosePlaylistMusicTracks()
{
    addPlaylistMusicTracks(QFileDialog::getOpenFileNames(
        this,
        uiText("Background Music"),
        {},
        backgroundMusicFileFilter()));
}

void BackgroundMusicWindow::chooseItemMusicTracks()
{
    addItemMusicTracks(QFileDialog::getOpenFileNames(
        this,
        uiText("Background Music"),
        {},
        backgroundMusicFileFilter()));
}

void BackgroundMusicWindow::addPlaylistMusicTracks(const QStringList& sources)
{
    bool added = false;
    bool skippedUnsupported = false;
    for (const QString& source : sources)
    {
        if (!isSupportedBackgroundMusicSource(source))
        {
            skippedUnsupported = true;
            continue;
        }

        added = workspaceModel_.addPlaylistMusicTrack(playlistId_, source).has_value() || added;
    }

    if (skippedUnsupported)
    {
        QMessageBox::warning(
            this,
            uiText("Background Music"),
            uiText("Unsupported audio files were skipped"));
    }
    if (added)
    {
        refreshPlaylistMusic();
        notifyQueueChanged(std::nullopt);
    }
}

void BackgroundMusicWindow::addItemMusicTracks(const QStringList& sources)
{
    if (!selectedItemBelongsToPlaylist())
    {
        return;
    }

    bool added = false;
    bool skippedUnsupported = false;
    for (const QString& source : sources)
    {
        if (!isSupportedBackgroundMusicSource(source))
        {
            skippedUnsupported = true;
            continue;
        }

        added = workspaceModel_.addItemMusicTrack(*selectedItemId_, source).has_value() || added;
    }

    if (skippedUnsupported)
    {
        QMessageBox::warning(
            this,
            uiText("Background Music"),
            uiText("Unsupported audio files were skipped"));
    }
    if (added)
    {
        refreshSelectedItemMusic();
        notifyQueueChanged(*selectedItemId_);
    }
}

void BackgroundMusicWindow::removePlaylistMusicTrack()
{
    const auto trackId = selectedTrackId(playlistTracks_);
    if (!trackId.has_value())
    {
        return;
    }

    const int removedRow = trackRow(playlistTracks_);
    if (workspaceModel_.removePlaylistMusicTrack(playlistId_, *trackId))
    {
        refreshPlaylistMusic(std::nullopt, removedRow);
        notifyQueueChanged(std::nullopt);
    }
}

void BackgroundMusicWindow::removeItemMusicTrack()
{
    const auto trackId = selectedTrackId(itemTracks_);
    if (!selectedItemBelongsToPlaylist() || !trackId.has_value())
    {
        return;
    }

    const int removedRow = trackRow(itemTracks_);
    if (workspaceModel_.removeItemMusicTrack(*selectedItemId_, *trackId))
    {
        refreshSelectedItemMusic(std::nullopt, removedRow);
        notifyQueueChanged(*selectedItemId_);
    }
}

void BackgroundMusicWindow::movePlaylistMusicTrack(int direction)
{
    const auto trackId = selectedTrackId(playlistTracks_);
    if (!trackId.has_value())
    {
        return;
    }

    if (workspaceModel_.movePlaylistMusicTrack(playlistId_, *trackId, direction))
    {
        refreshPlaylistMusic(*trackId);
        notifyQueueChanged(std::nullopt);
    }
}

void BackgroundMusicWindow::moveItemMusicTrack(int direction)
{
    const auto trackId = selectedTrackId(itemTracks_);
    if (!selectedItemBelongsToPlaylist() || !trackId.has_value())
    {
        return;
    }

    if (workspaceModel_.moveItemMusicTrack(*selectedItemId_, *trackId, direction))
    {
        refreshSelectedItemMusic(*trackId);
        notifyQueueChanged(*selectedItemId_);
    }
}

void BackgroundMusicWindow::notifyQueueChanged(std::optional<ItemId> itemId)
{
    if (queueChangedCallback_)
    {
        queueChangedCallback_(playlistId_, itemId);
    }
}

bool BackgroundMusicWindow::selectedItemBelongsToPlaylist() const
{
    if (!selectedItemId_.has_value())
    {
        return false;
    }

    const WorkspacePlaylist* playlist = workspaceModel_.playlist(playlistId_);
    if (playlist == nullptr)
    {
        return false;
    }

    return std::any_of(
        playlist->items.cbegin(),
        playlist->items.cend(),
        [this](const WorkspaceItem& item)
        {
            return item.id == *selectedItemId_;
        });
}

}
