#pragma once
#include <QWidget>

class QLabel;
class QMediaPlayer;
class QProcess;
class QPushButton;
class QSlider;

// Small player beside the link box: plays a low-resolution stream of the link so the
// user can mark where the clip starts and ends instead of typing times.
class PreviewPane : public QWidget {
  Q_OBJECT

 public:
  explicit PreviewPane(QWidget* parent = nullptr);
  // Resolves a stream for `url` with yt-dlp and loads it. Empty url clears the pane.
  void setUrl(const QString& url, const QString& ytdlp);

  static QString formatTime(qint64 ms);                 // 90500 → "1:30"
  static QString clipText(qint64 startMs, qint64 endMs);  // -1 = not set; "" when neither is set

 signals:
  void clipChosen(const QString& range);  // in the form keepr --clip accepts

 private:
  void mark(bool isStart);
  void updateTime();

  QMediaPlayer* player_;
  QProcess* resolver_ = nullptr;
  QSlider* slider_;
  QLabel* time_;
  QLabel* info_;
  QPushButton* playBtn_;
  QPushButton* startBtn_;
  QPushButton* endBtn_;
  QString url_;
  qint64 start_ = -1;
  qint64 end_ = -1;
};
