// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CUSTOMELEMENTS_H_
#define MUMBLE_MUMBLE_CUSTOMELEMENTS_H_

#include <QtCore/QObject>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QTextEdit>
#include <QTextObjectInterface>
#include <QtGui/QMovie>

Q_DECLARE_METATYPE(QMovie*)

class LogTextBrowser : public QTextBrowser {
private:
	Q_OBJECT
	Q_DISABLE_COPY(LogTextBrowser)

protected:
	void mousePressEvent(QMouseEvent *) Q_DECL_OVERRIDE;

public:
	LogTextBrowser(QWidget *p = nullptr);

	int getLogScroll();
	void setLogScroll(int scroll_pos);
	bool isScrolledToBottom();
};

class ChatbarTextEdit : public QTextEdit {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ChatbarTextEdit)
	void inFocus(bool);
	void applyPlaceholder();
	QStringList qslHistory;
	QString qsHistoryTemp;
	int iHistoryIndex;
	static const int MAX_HISTORY = 50;
	bool m_justPasted;

protected:
	QString qsDefaultText;
	bool bDefaultVisible;
	void focusInEvent(QFocusEvent *) Q_DECL_OVERRIDE;
	void focusOutEvent(QFocusEvent *) Q_DECL_OVERRIDE;
	void contextMenuEvent(QContextMenuEvent *) Q_DECL_OVERRIDE;
	void dragEnterEvent(QDragEnterEvent *) Q_DECL_OVERRIDE;
	void dragMoveEvent(QDragMoveEvent *) Q_DECL_OVERRIDE;
	void dropEvent(QDropEvent *) Q_DECL_OVERRIDE;
	bool event(QEvent *) Q_DECL_OVERRIDE;
	QSize minimumSizeHint() const Q_DECL_OVERRIDE;
	QSize sizeHint() const Q_DECL_OVERRIDE;
	void resizeEvent(QResizeEvent *e) Q_DECL_OVERRIDE;
	void insertFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
	bool sendImagesFromMimeData(const QMimeData *source);
	bool emitPastedImage(QImage image, QString filePath = "");

public:
	void setDefaultText(const QString &, bool = false);
	unsigned int completeAtCursor();
signals:
	void tabPressed(void);
	void backtabPressed(void);
	void ctrlSpacePressed(void);
	void entered(QString);
	void ctrlEnterPressed(QString);
	void pastedImage(QString);
public slots:
	void pasteAndSend_triggered();
	void doResize();
	void doScrollbar();
	void addToHistory(const QString &str);
	void historyUp();
	void historyDown();

public:
	ChatbarTextEdit(QWidget *p = nullptr);
};

class AnimationTextObject : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

protected:
	QSizeF intrinsicSize(QTextDocument *doc, int posInDoc, const QTextFormat &fmt) Q_DECL_OVERRIDE;
	void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int posInDoc, const QTextFormat &fmt) Q_DECL_OVERRIDE;

public:
	AnimationTextObject();
	static void mousePress(QAbstractTextDocumentLayout *docLayout, QPoint mouseDocPos, Qt::MouseButton button);
	static bool areVideoControlsOn;

	enum VideoController {
		VideoBar,
		View,
		PlayPause,
		CacheSwitch,
		LoopSwitch,
		PreviousFrame,
		NextFrame,
		ResetSpeed,
		DecreaseSpeed,
		IncreaseSpeed,
		None
	};
	enum LoopMode { Unchanged, Loop, NoLoop };
	static QString loopModeToString(LoopMode mode);

	static void setFrame(QMovie *animation, int frameIndex);
	static void updatePropertyPositionIfChanged(QObject *propertyHolder, QRectF rect);
	static void drawCenteredPlayIcon(QPainter *painter, QRectF rect);

	static bool isInBoundsOnAxis(QPoint pos, bool yInsteadOfX, int start, int length);
	static bool isInBounds(QPoint pos, QRectF bounds);
	static void setVideoControlPositioning(QObject *propertyHolder, QRectF rect, int videoBarHeight = 4, int underVideoBarHeight = 20,
	                                       int cacheX = -170, int loopModeX = -130, int frameTraversalX = -90, int speedX = -30);
	static VideoController mousePressVideoControls(QObject *propertyHolder, QPoint mouseDocPos);
	static void drawVideoControls(QPainter *painter, QObject *propertyHolder, QPixmap frame,
	                              bool isPaused, bool isCached, int frameIndex, int speed);
};

class DockTitleBar : public QLabel {
private:
	Q_OBJECT
	Q_DISABLE_COPY(DockTitleBar)
protected:
	QTimer *qtTick;
	int size;
	int newsize;

public:
	DockTitleBar();
	QSize sizeHint() const Q_DECL_OVERRIDE;
	QSize minimumSizeHint() const Q_DECL_OVERRIDE;
public slots:
	void tick();

protected:
	bool eventFilter(QObject *, QEvent *) Q_DECL_OVERRIDE;
};

#endif // CUSTOMELEMENTS_H_
