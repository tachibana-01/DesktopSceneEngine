#pragma once

#include <QUuid>

namespace DesktopSceneEngine::Player
{
enum class DocumentAction { New, Open, Save, SaveAs, Close };
enum class DocumentChoice { Save, Discard, Cancel };
enum class DocumentCommandStatus { Accepted, Busy, StaleOperation, Unavailable };

// Acknowledgement of native intent admission, not a persistence outcome.
struct DocumentCommandResult
{
    DocumentCommandStatus status = DocumentCommandStatus::Unavailable;
    QUuid token;
};
}
