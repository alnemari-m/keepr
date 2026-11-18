#include "Preview.hpp"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
  auto* video = new QVideoWidget(this);
  video->setMinimumSize(320, 180);
  player_ = new QMediaPlayer(this);
  player_->setAudioOutput(new QAudioOutput(this));
  player_->setVideoOutput(video);

  slider_ = new QSlider(Qt::Horizontal, this);
  slider_->setAccessibleName("Preview position");
  time_ = new QLabel(this);
  playBtn_ = new QPushButton("Play", this);
  startBtn_ = new QPushButton("Start here", this);
  endBtn_ = new QPushButton("End here", this);
  startBtn_->setToolTip("Download from the current position");
  endBtn_->setToolTip("Download up to the current position");
  info_ = new QLabel("Paste a video link to preview it here.", this);
  info_->setWordWrap(true);

  auto* seek = new QHBoxLayout;
  seek->addWidget(slider_, 1);
  seek->addWidget(time_);
  auto* btns = new QHBoxLayout;
  btns->addWidget(playBtn_);
  btns->addStretch();
  btns->addWidget(startBtn_);
  btns->addWidget(endBtn_);
  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->addWidget(video, 1);
  lay->addLayout(seek);
  lay->addLayout(btns);
  lay->addWidget(info_);

  connect(playBtn_, &QPushButton::clicked, this, [this] {
    player_->playbackState() == QMediaPlayer::PlayingState ? player_->pause() : player_->play();
  });
  connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
    playBtn_->setText(s == QMediaPlayer::PlayingState ? "Pause" : "Play");
  });
  connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
    slider_->setRange(0, static_cast<int>(d));
    updateTime();
  });
  connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
    if (!slider_->isSliderDown()) slider_->setValue(static_cast<int>(p));
    updateTime();
  });
  connect(slider_, &QSlider::sliderMoved, player_, &QMediaPlayer::setPosition);
  connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& msg) {
    info_->setText("Preview failed: " + msg);
  });
  connect(startBtn_, &QPushButton::clicked, this, [this] { mark(true); });
  connect(endBtn_, &QPushButton::clicked, this, [this] { mark(false); });
  setUrl({}, {});
}

QString PreviewPane::formatTime(qint64 ms) {
  qint64 s = ms / 1000;
  if (s >= 3600) return QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
  return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString PreviewPane::clipText(qint64 startMs, qint64 endMs) {
  if (startMs < 0 && endMs < 0) return {};
  return formatTime(qMax<qint64>(startMs, 0)) + "-" + (endMs < 0 ? QString("inf") : formatTime(endMs));
}

void PreviewPane::updateTime() {
  time_->setText(formatTime(player_->position()) + " / " + formatTime(player_->duration()));
}

void PreviewPane::mark(bool isStart) {
  qint64 pos = player_->position();
  (isStart ? start_ : end_) = pos;
  // marks are whole seconds, so compare seconds: an end at or before the start is dropped
  if (start_ >= 0 && end_ >= 0 && end_ / 1000 <= start_ / 1000) (isStart ? end_ : start_) = -1;
  QString c = clipText(start_, end_);
  info_->setText("Will download " + c.replace("-inf", " to the end").replace('-', " to "));
  emit clipChosen(clipText(start_, end_));
}

void PreviewPane::setUrl(const QString& url, const QString& ytdlp) {
  if (url == url_ && !url.isEmpty()) return;
  url_ = url;
  start_ = end_ = -1;
  player_->stop();
  player_->setSource({});
  slider_->setRange(0, 0);
  time_->clear();
  if (resolver_) {
    resolver_->disconnect(this);
    resolver_->kill();
    resolver_->deleteLater();
    resolver_ = nullptr;
  }
  for (QWidget* w : {static_cast<QWidget*>(slider_), static_cast<QWidget*>(playBtn_), static_cast<QWidget*>(startBtn_),
                     static_cast<QWidget*>(endBtn_)})
    w->setEnabled(false);
  if (url.isEmpty()) {
    info_->setText("Paste a video link to preview it here.");
    return;
  }
  info_->setText("Loading preview…");
  resolver_ = new QProcess(this);
  connect(resolver_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
    QString stream = QString::fromUtf8(resolver_->readAllStandardOutput()).section('\n', 0, 0).trimmed();
    resolver_->deleteLater();
    resolver_ = nullptr;
    if (code != 0 || !stream.startsWith("http")) {
      info_->setText("No preview for this link.");
      return;
    }
    for (QWidget* w : {static_cast<QWidget*>(slider_), static_cast<QWidget*>(playBtn_),
                       static_cast<QWidget*>(startBtn_), static_cast<QWidget*>(endBtn_)})
      w->setEnabled(true);
    info_->setText("Play or drag to a moment, then press Start here / End here.");
    player_->setSource(QUrl(stream));
    player_->pause();  // loads and shows the first frame without starting sound
  });
  connect(resolver_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart) info_->setText("No preview: yt-dlp was not found.");
  });
  // one small ready-to-play stream; falls back to video without sound, which is still enough to pick times
  resolver_->start(ytdlp.isEmpty() ? "yt-dlp" : ytdlp,
                   {"-g", "--no-playlist", "-f", "b[height<=480]/b/bv*[height<=480]/bv*", "--", url});
}
