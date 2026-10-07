#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <iostream>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    auto* inspectorPage = new QWidget();
    auto* mode = new QComboBox(inspectorPage);
    mode->addItem(QStringLiteral("Native"));
    mode->addItem(QStringLiteral("Auto Fill"));

    QPointer<QWidget> pageGuard{inspectorPage};
    bool presentationCommitted = false;
    bool refreshPending = false;
    int rebuildCount = 0;

    const auto queueInspectorRefresh = [&application, &pageGuard, &refreshPending, &rebuildCount]()
    {
        if (refreshPending)
        {
            return;
        }

        refreshPending = true;
        QTimer::singleShot(0, &application, [&pageGuard, &refreshPending, &rebuildCount]()
        {
            refreshPending = false;
            ++rebuildCount;
            delete pageGuard.data();
        });
    };

    QObject::connect(
        mode,
        QOverload<int>::of(&QComboBox::activated),
        &application,
        [&presentationCommitted, &queueInspectorRefresh](int index)
        {
            presentationCommitted = index == 0;
            queueInspectorRefresh();
        });

    mode->setCurrentIndex(0);
    mode->activated(0);
    mode->activated(0);

    if (!require(
            presentationCommitted,
            "the presentation mode must commit before the Inspector rebuild"))
    {
        delete inspectorPage;
        return 1;
    }
    if (!require(
            !pageGuard.isNull(),
            "the emitting Inspector page must survive until activated returns"))
    {
        return 1;
    }

    QCoreApplication::processEvents();
    if (!require(
            pageGuard.isNull(),
            "the deferred Inspector rebuild must occur after the signal event"))
    {
        return 1;
    }
    if (!require(
            rebuildCount == 1,
            "multiple mode notifications in one event turn must queue one rebuild"))
    {
        return 1;
    }

    return 0;
}
