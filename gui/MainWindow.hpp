#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

// Front end for the `keepr` command-line engine. Each URL runs as its own
// child process so output streams live into the log and Cancel can stop it.
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  void setUrls(const QStringList& urls);
  void startDownloads();

 protected:
  void closeEvent(QCloseEvent* e) override;
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dropEvent(QDropEvent* e) override;

 private slots:
  void onReadyRead();
  void onFinished(int exitCode, QProcess::ExitStatus status);
  void onCancel();
  void onBrowse();
  void onPaste();
  void openOutputFolder();
  void openResult(QListWidgetItem* item);

 private:
  void buildUi();
  void loadSettings();
  void saveSettings();
  void runNext();
  void handleLine(const QString& line);
  void setBusy(bool busy);
  void updateProgress();
  QStringList parseUrls() const;
  QString engineCommand() const;
  QString ytdlpPath() const;

  // widgets
  QPlainTextEdit* urlsEdit_;
  QLineEdit* outputEdit_;
  QComboBox* qualityCombo_;
  QComboBox* formatCombo_;
  QCheckBox* audioCheck_;
  QCheckBox* mediaCheck_;
  QPushButton* downloadBtn_;
  QPushButton* cancelBtn_;
  QProgressBar* progress_;
  QLabel* status_;
  QListWidget* results_;
  QPlainTextEdit* log_;

  // run state
  QProcess* proc_ = nullptr;
  QStringList queue_;
  int total_ = 0;
  int done_ = 0;
  int failed_ = 0;
  double currentFraction_ = 0.0;
  QString currentUrl_;
  QString currentResult_;
  QListWidgetItem* currentItem_ = nullptr;
  QString partialLine_;
  bool cancelled_ = false;
};
