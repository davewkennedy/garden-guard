// Garden Guard: a tower-defense game. Qt port of the HTML/JS version.
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <cstdio>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Used by QSettings to remember the sound on/off choice.
    QApplication::setOrganizationName("GardenGuard");
    QApplication::setApplicationName("Garden Guard PlusPlus");   // separate settings from the original

    // Hidden developer options (not listed in --help):
    //   --screenshot FILE   save an image of the window and quit
    //   --scene NAME        intro, play, placing, mapclear, win, lose (default: intro)
    //   --size WxH          window size, e.g. 1024x700
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption shotOpt("screenshot", "Save a screenshot and quit.", "file");
    QCommandLineOption sceneOpt("scene", "Scene to capture.", "name", "intro");
    QCommandLineOption sizeOpt("size", "Window size WxH.", "size");
    QCommandLineOption autoOpt("autopilot", "Start with the autopilot at this speed (1-3).", "speed");
    QCommandLineOption delayOpt("delay", "Milliseconds before the screenshot.", "ms", "800");
    QCommandLineOption mapOpt("start-map", "Start a fresh run on this map (1-based).", "n");
    for (QCommandLineOption *o : {&shotOpt, &sceneOpt, &sizeOpt, &autoOpt, &delayOpt, &mapOpt}) {
        o->setFlags(QCommandLineOption::HiddenFromHelp);
        parser.addOption(*o);
    }
    parser.process(app);

    MainWindow window;
    if (parser.isSet(sizeOpt)) {
        const QStringList wh = parser.value(sizeOpt).split('x');
        if (wh.size() == 2) window.resize(wh[0].toInt(), wh[1].toInt());
    }
    window.show();
    if (parser.isSet(mapOpt)) window.startAtMap(parser.value(mapOpt).toInt() - 1);
    if (parser.isSet(autoOpt)) window.startAutopilot(parser.value(autoOpt).toInt());

    if (parser.isSet(shotOpt)) {
        const QString file = parser.value(shotOpt);
        const QString scene = parser.value(sceneOpt);
        if (!window.setupScene(scene)) {
            std::fprintf(stderr, "unknown scene: %s\n", qPrintable(scene));
            return 2;
        }
        // Give layouts and the first frames a moment to settle, then capture.
        QTimer::singleShot(parser.value(delayOpt).toInt(), &window, [&window, file] {
            const bool ok = window.grab().save(file);
            std::printf("%s %s (%dx%d)\n", ok ? "saved" : "FAILED to save", qPrintable(file),
                        window.width(), window.height());
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
