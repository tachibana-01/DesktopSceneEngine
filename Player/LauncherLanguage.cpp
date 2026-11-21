#include "LauncherLanguage.h"

#include <QJsonArray>
#include <QMetaObject>
#include <QWidget>

#include <iostream>
#include <stdexcept>
#include <utility>

namespace DesktopSceneEngine::Player
{
class LauncherLanguage::Receiver final : public QWidget
{
public:
    explicit Receiver(LauncherLanguage& owner) : owner_(owner)
    {
        setObjectName(QStringLiteral("DSELauncherLanguageInvalidationReceiver"));
        setAttribute(Qt::WA_DontShowOnScreen);
#ifdef _WIN32
        message_ = globalSettingsChangedMessageId();
        if (message_ == 0)
            std::cerr << "[Player] Global Settings invalidation registration failed; live Launcher Language updates unavailable.\n";
        // HWND_BROADCAST reaches top-level windows, including hidden ones. A
        // message-only HWND would silently miss production invalidation.
        (void)winId();
#endif
    }

    bool nativeEvent(const QByteArray& type, void* nativeData, qintptr* result) override
    {
#ifdef _WIN32
        if (type == "windows_generic_MSG" && nativeData)
        {
            const auto* message = static_cast<const MSG*>(nativeData);
            if (message_ != 0 && message->message == message_)
            {
                // This is a zero-payload hint; read current persisted truth only
                // after native dispatch, and cancel work if the owner dies.
                QMetaObject::invokeMethod(&owner_, [owner = &owner_]
                { owner->consumeInvalidation(); }, Qt::QueuedConnection);
                if (result) *result = 0;
                return true;
            }
        }
#endif
        return QWidget::nativeEvent(type, nativeData, result);
    }

private:
    LauncherLanguage& owner_;
    std::uint32_t message_ = 0;
};

LauncherLanguage::LauncherLanguage(ApplicationPreferencesStore& store,
    ApplicationTranslationState& translation, std::function<bool()> publish)
    : store_(store), translation_(translation), publish_(std::move(publish))
{
    receiver_ = std::make_unique<Receiver>(*this);
    (void)reload();
}

LauncherLanguage::~LauncherLanguage() = default;

const LauncherLanguageProjection& LauncherLanguage::current() const noexcept { return projection_; }

LauncherLanguageProjection LauncherLanguage::reload()
{
    const auto committed = store_.reloadLanguage();
    const auto effective = committed ? translation_.setLanguage(*committed)
        : translation_.activeLanguage();
    return update(committed, effective);
}

void LauncherLanguage::consumeInvalidation() { (void)reload(); }

QJsonObject LauncherLanguage::commit(UiLanguage requested)
{
    if (requested != UiLanguage::English && requested != UiLanguage::SimplifiedChinese)
        throw std::invalid_argument("Unsupported Launcher language");

    const auto result = [](const char* status, const LauncherLanguageProjection& language,
        const QJsonArray& warnings = QJsonArray{})
    {
        return QJsonObject{{QStringLiteral("status"), QLatin1String{status}},
            {QStringLiteral("language"), serializeLauncherLanguage(language)},
            {QStringLiteral("warnings"), warnings}};
    };
    const auto committed = store_.reloadLanguage();
    if (!committed)
        return result("READ_FAILURE", update(std::nullopt, translation_.activeLanguage()));
    if (*committed == requested)
        return result("UNCHANGED", update(committed, translation_.activeLanguage()));
    if (store_.saveLanguage(requested) != QSettings::NoError)
    {
        // A failed QSettings operation may retain a pending value in Qt's
        // process cache. Do not reread it and mistake that attempt for a commit.
        return result("SAVE_FAILURE", update(committed, translation_.activeLanguage()));
    }

    const auto effective = translation_.setLanguage(requested);
    const auto transaction = update(requested, effective);
    const bool published = publish_ && publish_();
    QJsonArray warnings;
    if (effective != requested) warnings.append(QStringLiteral("TranslationFallback"));
    if (!published) warnings.append(QStringLiteral("NotificationFailure"));
    // Keep the transaction's own revision even if publication dispatched a
    // newer invalidation. The page can reject this older result by revision.
    return result("COMMITTED", transaction, warnings);
}

void LauncherLanguage::setOnChanged(
    std::function<void(const LauncherLanguageProjection&)> callback)
{
    onChanged_ = std::move(callback);
}

LauncherLanguageProjection LauncherLanguage::update(
    std::optional<UiLanguage> committed, UiLanguage effective)
{
    if (projection_.committed == committed && projection_.effective == effective)
        return projection_;
    projection_ = {committed, effective, projection_.revision + 1};
    const auto changed = projection_;
    if (const auto callback = onChanged_) callback(changed);
    return changed;
}
}
