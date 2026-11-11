#include "MainWindow.hpp"

#include <signal.h>
#include <unistd.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const QRegularExpression kPercent(R"(\[download\]\s+([0-9.]+)%)");
const QRegularExpression kDestination(R"(\[download\] Destination: (.+)$)");
const QRegularExpression kAlready(R"(\[download\] (.+) has already been downloaded)");
const QRegularExpression kMerged(R"(\[(?:Merger|ExtractAudio)\] (?:Merging formats into|Destination:) \"?(.+?)\"?$)");
const QRegularExpression kSaved(R"(saved \d+ media file\(s\) → (.+)$)");
const QRegularExpression kAnsi(R"(\x1b\[[0-9;]*[A-Za-z])");

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("Keepr");
  setWindowIcon(QIcon(":/icons/keepr.png"));
  setAcceptDrops(true);
  buildUi();
  loadSettings();
  setBusy(false);
  resize(860, 720);
}

MainWindow::~MainWindow() {
  if (proc_ && proc_->state() != QProcess::NotRunning) {
    ::kill(-static_cast<pid_t>(proc_->processId()), SIGTERM);
    proc_->waitForFinished(2000);
  }
}

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);
  root->setContentsMargins(14, 14, 14, 10);
  root->setSpacing(10);

  // --- links ---
  auto* linksBox = new QGroupBox("Links", central);
  auto* linksLay = new QVBoxLayout(linksBox);
  urlsEdit_ = new QPlainTextEdit(linksBox);
  urlsEdit_->setPlaceholderText(
      "Paste one or more links, one per line — or drag them here.\n\n"
      "X / Twitter posts and Articles (text, images, code, video)\n"
      "TikTok, YouTube, Instagram, Reddit, Vimeo and ~1800 other video sites");
  urlsEdit_->setMinimumHeight(90);
  urlsEdit_->setMaximumHeight(150);
  auto* linkBtns = new QHBoxLayout;
  auto* pasteBtn = new QPushButton(QIcon::fromTheme("edit-paste"), "Paste", linksBox);
  auto* clearBtn = new QPushButton(QIcon::fromTheme("edit-clear"), "Clear", linksBox);
  linkBtns->addWidget(pasteBtn);
  linkBtns->addWidget(clearBtn);
  linkBtns->addStretch();
  linksLay->addWidget(urlsEdit_);
  linksLay->addLayout(linkBtns);
  connect(pasteBtn, &QPushButton::clicked, this, &MainWindow::onPaste);
  connect(clearBtn, &QPushButton::clicked, urlsEdit_, &QPlainTextEdit::clear);

  // --- options ---
  auto* optBox = new QGroupBox("Options", central);
  auto* form = new QFormLayout(optBox);
  auto* outRow = new QHBoxLayout;
  outputEdit_ = new QLineEdit(optBox);
  auto* browseBtn = new QPushButton(QIcon::fromTheme("folder-open"), "Browse…", optBox);
  outRow->addWidget(outputEdit_, 1);
  outRow->addWidget(browseBtn);
  form->addRow("Save to:", outRow);
  connect(browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowse);

  auto* row2 = new QHBoxLayout;
  qualityCombo_ = new QComboBox(optBox);
  qualityCombo_->addItem("Best available", 0);
  for (int h : {2160, 1440, 1080, 720, 480, 360}) qualityCombo_->addItem(QString("Up to %1p").arg(h), h);
  audioCheck_ = new QCheckBox("Audio only (mp3)", optBox);
  row2->addWidget(qualityCombo_);
  row2->addSpacing(12);
  row2->addWidget(audioCheck_);
  row2->addStretch();
  form->addRow("Video quality:", row2);
  connect(audioCheck_, &QCheckBox::toggled, qualityCombo_, &QComboBox::setDisabled);

  auto* row3 = new QHBoxLayout;
  formatCombo_ = new QComboBox(optBox);
  formatCombo_->addItem("Markdown + HTML", "both");
  formatCombo_->addItem("Markdown only", "md");
  formatCombo_->addItem("HTML only", "html");
  mediaCheck_ = new QCheckBox("Download images && videos", optBox);
  mediaCheck_->setChecked(true);
  row3->addWidget(formatCombo_);
  row3->addSpacing(12);
  row3->addWidget(mediaCheck_);
  row3->addStretch();
  form->addRow("X articles as:", row3);

  // --- actions ---
  auto* actions = new QHBoxLayout;
  downloadBtn_ = new QPushButton(QIcon::fromTheme("download", QIcon::fromTheme("go-down")), "Download", central);
  downloadBtn_->setDefault(true);
  downloadBtn_->setMinimumHeight(34);
  downloadBtn_->setMinimumWidth(140);
  QFont bf = downloadBtn_->font();
  bf.setBold(true);
  downloadBtn_->setFont(bf);
  cancelBtn_ = new QPushButton(QIcon::fromTheme("process-stop"), "Cancel", central);
  auto* openBtn = new QPushButton(QIcon::fromTheme("folder"), "Open folder", central);
  actions->addWidget(downloadBtn_);
  actions->addWidget(cancelBtn_);
  actions->addStretch();
  actions->addWidget(openBtn);
  connect(downloadBtn_, &QPushButton::clicked, this, &MainWindow::startDownloads);
  connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::onCancel);
  connect(openBtn, &QPushButton::clicked, this, &MainWindow::openOutputFolder);

  progress_ = new QProgressBar(central);
  progress_->setRange(0, 1000);
  progress_->setTextVisible(false);
  status_ = new QLabel("Ready", central);

  // --- results + log ---
  results_ = new QListWidget(central);
  results_->setToolTip("Double-click to open");
  connect(results_, &QListWidget::itemDoubleClicked, this, &MainWindow::openResult);
  log_ = new QPlainTextEdit(central);
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(5000);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  log_->setPlaceholderText("Log");
  auto* split = new QSplitter(Qt::Vertical, central);
  split->addWidget(results_);
  split->addWidget(log_);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 2);

  root->addWidget(linksBox);
  root->addWidget(optBox);
  root->addLayout(actions);
  root->addWidget(progress_);
  root->addWidget(status_);
  root->addWidget(split, 1);
  setCentralWidget(central);
}

void MainWindow::loadSettings() {
  QSettings s;
  QString def = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/keepr";
  outputEdit_->setText(s.value("output", def).toString());
  qualityCombo_->setCurrentIndex(qMax(0, qualityCombo_->findData(s.value("quality", 0))));
  formatCombo_->setCurrentIndex(qMax(0, formatCombo_->findData(s.value("format", "both"))));
  audioCheck_->setChecked(s.value("audio", false).toBool());
  mediaCheck_->setChecked(s.value("media", true).toBool());
  restoreGeometry(s.value("geometry").toByteArray());
}

void MainWindow::saveSettings() {
  QSettings s;
  s.setValue("output", outputEdit_->text());
  s.setValue("quality", qualityCombo_->currentData());
  s.setValue("format", formatCombo_->currentData());
  s.setValue("audio", audioCheck_->isChecked());
  s.setValue("media", mediaCheck_->isChecked());
  s.setValue("geometry", saveGeometry());
}

void MainWindow::setUrls(const QStringList& urls) { urlsEdit_->setPlainText(urls.join('\n')); }

QStringList MainWindow::parseUrls() const {
  QStringList out;
  static const QRegularExpression ws(R"(\s+)");
  for (const QString& tok : urlsEdit_->toPlainText().split(ws, Qt::SkipEmptyParts)) {
    QString t = tok.trimmed();
    if (t.startsWith('#')) continue;
    if (t.contains('.') && !out.contains(t)) out << t;
  }
  return out;
}

QString MainWindow::engineCommand() const {
  // Prefer the engine installed next to this GUI, then PATH.
  QString sibling = QCoreApplication::applicationDirPath() + "/keepr";
  if (QFileInfo(sibling).isExecutable()) return sibling;
  return QStandardPaths::findExecutable("keepr");
}

QString MainWindow::ytdlpPath() const {
  QString env = qEnvironmentVariable("KEEPR_YTDLP");
  if (!env.isEmpty()) return env;
  QString venv = QDir::homePath() + "/.venvs/yt-dlp/bin/yt-dlp";  // recent build with impersonation
  if (QFileInfo(venv).isExecutable()) return venv;
  return {};
}

void MainWindow::startDownloads() {
  if (proc_) return;
  QStringList urls = parseUrls();
  if (urls.isEmpty()) {
    status_->setText("Paste at least one link first.");
    urlsEdit_->setFocus();
    return;
  }
  if (engineCommand().isEmpty()) {
    QMessageBox::critical(this, "Keepr",
                          "The Keepr engine was not found next to this program or on PATH.\n"
                          "Build and install it with: cmake --install build --prefix ~/.local");
    return;
  }
  QDir().mkpath(outputEdit_->text());
  saveSettings();
  queue_ = urls;
  total_ = urls.size();
  done_ = failed_ = 0;
  cancelled_ = false;
  log_->clear();
  setBusy(true);
  runNext();
}

void MainWindow::runNext() {
  if (queue_.isEmpty() || cancelled_) {
    setBusy(false);
    progress_->setValue(cancelled_ ? progress_->value() : 1000);
    QString msg = cancelled_ ? "Cancelled." : QString("Finished: %1 succeeded, %2 failed.").arg(done_ - failed_).arg(failed_);
    status_->setText(msg);
    statusBar()->showMessage(msg, 10000);
    return;
  }
  currentUrl_ = queue_.takeFirst();
  currentResult_.clear();
  currentFraction_ = 0.0;
  partialLine_.clear();
  currentItem_ = new QListWidgetItem(QIcon::fromTheme("content-loading", QIcon::fromTheme("view-refresh")),
                                     "Downloading…  " + currentUrl_, results_);
  results_->scrollToItem(currentItem_);
  status_->setText(QString("Downloading %1 of %2…").arg(done_ + 1).arg(total_));
  updateProgress();

  QStringList args{"-o", outputEdit_->text()};
  int q = qualityCombo_->currentData().toInt();
  if (audioCheck_->isChecked()) args << "-a";
  else if (q > 0) args << "-q" << QString::number(q);
  if (!mediaCheck_->isChecked()) args << "--no-media";
  args << "--format" << formatCombo_->currentData().toString();
  QString y = ytdlpPath();
  if (!y.isEmpty()) args << "--yt-dlp" << y;
  args << currentUrl_;

  log_->appendPlainText("$ keepr " + args.join(' '));
  proc_ = new QProcess(this);
  proc_->setProcessChannelMode(QProcess::MergedChannels);
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("PYTHONUNBUFFERED", "1");  // stream yt-dlp progress through the pipe
  proc_->setProcessEnvironment(env);
  // Own process group, so Cancel stops the engine *and* the yt-dlp it spawned.
  proc_->setChildProcessModifier([] { ::setsid(); });
  connect(proc_, &QProcess::readyRead, this, &MainWindow::onReadyRead);
  connect(proc_, &QProcess::finished, this, &MainWindow::onFinished);
  connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart) {
      log_->appendPlainText("error: could not start the keepr engine: " + proc_->errorString());
      onFinished(127, QProcess::CrashExit);
    }
  });
  proc_->start(engineCommand(), args);
}

void MainWindow::onReadyRead() {
  if (!proc_) return;
  partialLine_ += QString::fromUtf8(proc_->readAll());
  partialLine_.replace('\r', '\n');
  int nl;
  while ((nl = partialLine_.indexOf('\n')) >= 0) {
    QString line = partialLine_.left(nl);
    partialLine_.remove(0, nl + 1);
    handleLine(line.remove(kAnsi));
  }
}

void MainWindow::handleLine(const QString& line) {
  if (line.trimmed().isEmpty()) return;
  if (auto m = kPercent.match(line); m.hasMatch()) {
    currentFraction_ = qBound(0.0, m.captured(1).toDouble() / 100.0, 1.0);
    updateProgress();
    // keep the log readable: replace consecutive progress lines instead of appending
    QTextCursor c = log_->textCursor();
    c.movePosition(QTextCursor::End);
    c.select(QTextCursor::BlockUnderCursor);
    if (kPercent.match(c.selectedText()).hasMatch()) {
      c.removeSelectedText();
      log_->appendPlainText(line);
      return;
    }
  }
  if (auto m = kSaved.match(line); m.hasMatch()) currentResult_ = m.captured(1).trimmed();
  else if (auto m2 = kMerged.match(line); m2.hasMatch()) currentResult_ = m2.captured(1).trimmed();
  else if (auto m3 = kDestination.match(line); m3.hasMatch()) currentResult_ = m3.captured(1).trimmed();
  else if (auto m4 = kAlready.match(line); m4.hasMatch()) currentResult_ = m4.captured(1).trimmed();
  log_->appendPlainText(line);
}

void MainWindow::onFinished(int exitCode, QProcess::ExitStatus status) {
  if (!proc_) return;
  onReadyRead();
  if (!partialLine_.isEmpty()) handleLine(partialLine_);
  proc_->deleteLater();
  proc_ = nullptr;

  const bool ok = !cancelled_ && status == QProcess::NormalExit && exitCode == 0;
  ++done_;
  if (!ok) ++failed_;
  if (currentItem_) {
    if (ok) {
      QString shown = currentResult_.isEmpty() ? currentUrl_ : QFileInfo(currentResult_).fileName();
      currentItem_->setText("✔  " + shown);
      currentItem_->setIcon(QIcon::fromTheme("emblem-ok", QIcon::fromTheme("dialog-ok")));
    } else {
      currentItem_->setText((cancelled_ ? "■  Cancelled  " : "✖  Failed  ") + currentUrl_);
      currentItem_->setIcon(QIcon::fromTheme("dialog-error"));
    }
    currentItem_->setData(Qt::UserRole, currentResult_);
    currentItem_->setToolTip(currentResult_.isEmpty() ? currentUrl_ : currentResult_);
  }
  currentFraction_ = 0.0;
  runNext();
}

void MainWindow::updateProgress() {
  if (total_ <= 0) return;
  progress_->setValue(static_cast<int>(1000.0 * (done_ + currentFraction_) / total_));
}

void MainWindow::onCancel() {
  cancelled_ = true;
  queue_.clear();
  if (proc_ && proc_->state() != QProcess::NotRunning) {
    log_->appendPlainText("— cancelling —");
    ::kill(-static_cast<pid_t>(proc_->processId()), SIGTERM);  // whole group: engine + yt-dlp
  } else {
    setBusy(false);
  }
}

void MainWindow::setBusy(bool busy) {
  downloadBtn_->setEnabled(!busy);
  cancelBtn_->setEnabled(busy);
  urlsEdit_->setReadOnly(busy);
  if (!busy) progress_->setRange(0, 1000);
}

void MainWindow::onBrowse() {
  QString dir = QFileDialog::getExistingDirectory(this, "Save downloads to", outputEdit_->text());
  if (!dir.isEmpty()) outputEdit_->setText(dir);
}

void MainWindow::onPaste() {
  QString clip = QApplication::clipboard()->text().trimmed();
  if (clip.isEmpty()) return;
  QString cur = urlsEdit_->toPlainText().trimmed();
  urlsEdit_->setPlainText(cur.isEmpty() ? clip : cur + "\n" + clip);
}

void MainWindow::openOutputFolder() {
  QDir().mkpath(outputEdit_->text());
  QDesktopServices::openUrl(QUrl::fromLocalFile(outputEdit_->text()));
}

void MainWindow::openResult(QListWidgetItem* item) {
  QString path = item->data(Qt::UserRole).toString();
  if (path.isEmpty()) return;
  QFileInfo fi(path);
  if (fi.isDir()) {
    // Articles and posts: open the readable page if there is one.
    for (const char* page : {"article.html", "post.html"}) {
      if (QFileInfo::exists(fi.filePath() + "/" + page)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.filePath() + "/" + page));
        return;
      }
    }
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(fi.exists() ? fi.filePath() : fi.path()));
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (proc_) {
    if (QMessageBox::question(this, "Keepr", "A download is running. Stop it and quit?") != QMessageBox::Yes) {
      e->ignore();
      return;
    }
    onCancel();
  }
  saveSettings();
  e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasUrls() || e->mimeData()->hasText()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
  QStringList add;
  if (e->mimeData()->hasUrls()) {
    for (const QUrl& u : e->mimeData()->urls()) add << u.toString();
  } else {
    add << e->mimeData()->text().trimmed();
  }
  QString cur = urlsEdit_->toPlainText().trimmed();
  urlsEdit_->setPlainText((cur.isEmpty() ? "" : cur + "\n") + add.join('\n'));
  e->acceptProposedAction();
}
