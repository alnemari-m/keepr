// keepr-gui — Qt 6 desktop front end for the keepr engine.
//
//   keepr-gui [URL...]            prefill links
//   keepr-gui --start URL...      prefill and start downloading immediately
//   keepr-gui --screenshot F.png  save a window screenshot after --screenshot-delay ms and quit
//                                     (used for automated UI checks)

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include "MainWindow.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("keepr");
  QApplication::setOrganizationName("keepr");
  QApplication::setApplicationVersion("0.1.0");
  QApplication::setDesktopFileName("keepr");

  QCommandLineParser p;
  p.setApplicationDescription("Download videos and archive X Articles.");
  p.addHelpOption();
  p.addVersionOption();
  QCommandLineOption start("start", "Start downloading the given links immediately.");
  QCommandLineOption shot("screenshot", "Save a screenshot to <file> and quit.", "file");
  QCommandLineOption delay("screenshot-delay", "Milliseconds before the screenshot (default 800).", "ms", "800");
  p.addOptions({start, shot, delay});
  p.addPositionalArgument("urls", "Links to download.", "[URL...]");
  p.process(app);

  MainWindow w;
  if (!p.positionalArguments().isEmpty()) w.setUrls(p.positionalArguments());
  w.show();
  if (p.isSet(start)) QTimer::singleShot(0, &w, &MainWindow::startDownloads);

  if (p.isSet(shot)) {
    const QString file = p.value(shot);
    QTimer::singleShot(p.value(delay).toInt(), &w, [&w, file] {
      w.grab().save(file);
      QApplication::quit();
    });
  }
  return app.exec();
}
