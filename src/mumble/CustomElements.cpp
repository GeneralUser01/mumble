// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "CustomElements.h"

#include "ClientUser.h"
#include "Log.h"
#include "MainWindow.h"
#include "QtWidgetUtils.h"
#include "Utils.h"
#include "Global.h"

#include <QMimeData>
#include <QtCore/QTimer>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMovie>
#include <QtWidgets/QScrollBar>

LogTextBrowser::LogTextBrowser(QWidget *p) : QTextBrowser(p) {
}

int LogTextBrowser::getLogScroll() {
	return verticalScrollBar()->value();
}

void LogTextBrowser::setLogScroll(int scroll_pos) {
	verticalScrollBar()->setValue(scroll_pos);
}

bool LogTextBrowser::isScrolledToBottom() {
	const QScrollBar *scrollBar = verticalScrollBar();
	return scrollBar->value() == scrollBar->maximum();
}

void LogTextBrowser::mousePressEvent(QMouseEvent *mouseEvt) {
	QPoint mouseDocPos = mouseEvt->pos();
	Qt::MouseButton mouseButton = mouseEvt->button();
	QAbstractTextDocumentLayout *docLayout = document()->documentLayout();
	// Set the vertical axis of the position to include the scrollable area above it:
	mouseDocPos.setY(mouseDocPos.y() + verticalScrollBar()->value());

	AnimationTextObject::mousePress(docLayout, mouseDocPos, mouseButton);
}


void ChatbarTextEdit::focusInEvent(QFocusEvent *qfe) {
	inFocus(true);
	QTextEdit::focusInEvent(qfe);
}

void ChatbarTextEdit::focusOutEvent(QFocusEvent *qfe) {
	inFocus(false);
	QTextEdit::focusOutEvent(qfe);
}

void ChatbarTextEdit::inFocus(bool focus) {
	if (focus) {
		if (bDefaultVisible) {
			QFont f = font();
			f.setItalic(false);
			setFont(f);
			setPlainText(QString());
			setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
			bDefaultVisible = false;
		}
	} else {
		if (toPlainText().trimmed().isEmpty() || bDefaultVisible) {
			applyPlaceholder();
		} else {
			setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
			bDefaultVisible = false;
		}
	}
}

void ChatbarTextEdit::contextMenuEvent(QContextMenuEvent *qcme) {
	QMenu *menu = createStandardContextMenu();

	QAction *action = new QAction(tr("Paste and &Send") + QLatin1Char('\t'), menu);
	action->setShortcut(static_cast< int >(Qt::CTRL) | Qt::Key_Shift | Qt::Key_V);
	action->setEnabled(!QApplication::clipboard()->text().isEmpty());
	connect(action, SIGNAL(triggered()), this, SLOT(pasteAndSend_triggered()));
	if (menu->actions().count() > 6)
		menu->insertAction(menu->actions()[6], action);
	else
		menu->addAction(action);

	menu->exec(qcme->globalPos());
	delete menu;
}

void ChatbarTextEdit::dragEnterEvent(QDragEnterEvent *evt) {
	inFocus(true);
	evt->acceptProposedAction();
}

void ChatbarTextEdit::dragMoveEvent(QDragMoveEvent *evt) {
	inFocus(true);
	evt->acceptProposedAction();
}

void ChatbarTextEdit::dropEvent(QDropEvent *evt) {
	inFocus(true);
	if (sendImagesFromMimeData(evt->mimeData())) {
		evt->acceptProposedAction();
	} else {
		QTextEdit::dropEvent(evt);
	}
}

ChatbarTextEdit::ChatbarTextEdit(QWidget *p) : QTextEdit(p), iHistoryIndex(-1) {
	setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setMinimumHeight(0);
	connect(this, &ChatbarTextEdit::textChanged, this, &ChatbarTextEdit::doResize);

	bDefaultVisible = true;
	setDefaultText(tr("<center>Type chat message here</center>"));
	setAcceptDrops(true);

	m_justPasted = false;
}

QSize ChatbarTextEdit::minimumSizeHint() const {
	return QSize(0, fontMetrics().height());
}

QSize ChatbarTextEdit::sizeHint() const {
	QSize sh                    = QTextEdit::sizeHint();
	const int minHeight         = minimumSizeHint().height();
	const int documentHeight    = static_cast< int >(document()->documentLayout()->documentSize().height());
	const int chatBarLineHeight = QFontMetrics(ChatbarTextEdit::font()).height();

	sh.setHeight(std::max(minHeight, std::min(chatBarLineHeight * 10, documentHeight)));
	const_cast< ChatbarTextEdit * >(this)->setMaximumHeight(sh.height());
	return sh;
}

void ChatbarTextEdit::resizeEvent(QResizeEvent *e) {
	QTextEdit::resizeEvent(e);
	QTimer::singleShot(0, this, SLOT(doScrollbar()));

	if (bDefaultVisible) {
		QTimer::singleShot(0, [this]() { applyPlaceholder(); });
	}
}

void ChatbarTextEdit::doResize() {
	updateGeometry();
	QTimer::singleShot(0, this, SLOT(doScrollbar()));
}

void ChatbarTextEdit::doScrollbar() {
	const int documentHeight = static_cast< int >(document()->documentLayout()->documentSize().height());
	setVerticalScrollBarPolicy(documentHeight > height() ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
	ensureCursorVisible();
}

void ChatbarTextEdit::setDefaultText(const QString &new_default, bool force) {
	qsDefaultText = new_default;

	if (bDefaultVisible || force) {
		applyPlaceholder();
	}
}

void ChatbarTextEdit::applyPlaceholder() {
	QFont f = font();
	f.setItalic(true);
	setFont(f);
	setWordWrapMode(QTextOption::NoWrap);
	setHtml(qsDefaultText);

	Mumble::QtUtils::elideText(*document(), static_cast< uint32_t >(size().width()));

	bDefaultVisible = true;
}

void ChatbarTextEdit::insertFromMimeData(const QMimeData *source) {
	if (!sendImagesFromMimeData(source)) {
		QTextEdit::insertFromMimeData(source);
	}
}

bool ChatbarTextEdit::sendImagesFromMimeData(const QMimeData *source) {
	if ((source->hasImage() || source->hasUrls())) {
		if (Global::get().bAllowHTML) {
			if (source->hasImage()) {
				// Process the image pasted onto the chatbar.
				QImage image = qvariant_cast< QImage >(source->imageData());
				if (emitPastedImage(image)) {
					return true;
				} else {
					Global::get().l->log(Log::Information, tr("Unable to send image: too large."));
					return false;
				}

			} else if (source->hasUrls()) {
				// Process the files dropped onto the chatbar. URLs here should be understood as the URIs of files.
				QList< QUrl > urlList = source->urls();

				int count = 0;
				for (int i = 0; i < urlList.size(); ++i) {
					QString path = urlList[i].toLocalFile();
					QImage image(path);

					if (image.isNull())
						continue;
					if (emitPastedImage(image, path)) {
						++count;
					} else {
						Global::get().l->log(Log::Information, tr("Unable to send image %1: too large.").arg(path));
					}
				}

				return (count > 0);
			}
		} else {
			Global::get().l->log(Log::Information, tr("This server does not allow sending images."));
		}
	}
	return false;
}

bool ChatbarTextEdit::emitPastedImage(QImage image, QString filePath) {
	if (filePath.endsWith(".gif")) {
		QFile file(filePath);
		if (file.open(QIODevice::ReadOnly)) {
			QByteArray animationBa(file.readAll());
			file.close();
			QString base64ImageData = qvariant_cast< QString >(animationBa.toBase64());
			emit pastedImage("<br /><img src=\"data:image/GIF;base64," + base64ImageData + "\" />");
		} else {
			Global::get().l->log(Log::Information, tr("Unable to read animated image file: %1").arg(filePath));
		}
		return true;
	}

	QString processedImage = Log::imageToImg(image, static_cast< int >(Global::get().uiImageLength));
	if (processedImage.length() > 0) {
		QString imgHtml = QLatin1String("<br />") + processedImage;
		emit pastedImage(imgHtml);
		return true;
	}
	return false;
}


bool AnimationTextObject::areVideoControlsOn = false;

void AnimationTextObject::setFrame(QMovie *animation, int frameIndex) {
	int lastFrameIndex = animation->property("lastFrameIndex").toInt();
	bool isFrameIndexTooLow = frameIndex < 0;
	bool isFrameIndexTooHigh = frameIndex > lastFrameIndex;
	if (isFrameIndexTooLow || isFrameIndexTooHigh) {
		frameIndex = isFrameIndexTooLow ? 0 : lastFrameIndex;
	}
	if (animation->cacheMode() == QMovie::CacheAll) {
		animation->jumpToFrame(frameIndex);
		return;
	}

	bool wasRunning = animation->state() == QMovie::Running;
	if (!wasRunning) {
		animation->setPaused(false);
	}
	bool isStartTried = false;
	// Can only load the target frame by traversing
	// in sequential order when the frames are not cached:
	while (animation->currentFrameNumber() != frameIndex) {
		if (!animation->jumpToNextFrame()) {
			// Continue traversing animations that either are stopped or do stop after one or more iterations:
			if (animation->state() == QMovie::NotRunning && !isStartTried) {
				animation->start();
				isStartTried = true;
				continue;
			}
			break;
		}
	}
	if (!wasRunning) {
		animation->setPaused(true);
	}
}

QString AnimationTextObject::loopModeToString(LoopMode mode) {
	switch (mode) {
		case Unchanged: return "Unchanged";
		case Loop:      return "Loop";
		case NoLoop:    return "No loop";
		default:        return "Undefined";
	}
}

void AnimationTextObject::drawCenteredPlayIcon(QPainter *painter, QRectF rect) {
	int centerX = rect.x() + rect.width() / 2;
	int centerY = rect.y() + rect.height() / 2;
	// Add a play-icon, which is a right-pointing triangle, like this "▶":
	QPolygonF polygon({
		QPointF(centerX - 8, centerY - 10),
		QPointF(centerX + 12, centerY),
		QPointF(centerX - 8, centerY + 10)
	});
	QPainterPath path;
	QPen thinBlackPen(Qt::black, 0.25);
	path.addPolygon(polygon);
	painter->fillPath(path, QBrush(Qt::white));
	// Add outline contrast to the triangle:
	painter->strokePath(path, thinBlackPen);

	auto drawCenteredCircle = [painter, centerX, centerY](int diameter) {
		int radius = diameter / 2;
		painter->drawEllipse(centerX - radius, centerY - radius, diameter, diameter);
	};
	// Add a ring around the triangle:
	painter->setPen(QPen(Qt::white, 2));
	drawCenteredCircle(40);
	// Add outline contrast to the ring:
	painter->setPen(thinBlackPen);
	drawCenteredCircle(36);
	drawCenteredCircle(44);
}

void AnimationTextObject::updatePropertyPositionIfChanged(QObject *propertyHolder, QRectF rect) {
	QRectF propertyRect = propertyHolder->property("posAndSize").toRectF();
	// Update the position in case content above it has increased in height, such as by text wrapping:
	if (propertyRect.y() != rect.y()) {
		propertyHolder->setProperty("posAndSize", QVariant(rect));
	}
}

bool AnimationTextObject::isInBoundsOnAxis(QPoint pos, bool yInsteadOfX, int start, int length) {
	int posOnAxis = yInsteadOfX ? pos.y() : pos.x();
	return posOnAxis >= start && posOnAxis <= start + length;
}

bool AnimationTextObject::isInBounds(QPoint pos, QRectF bounds) {
	return isInBoundsOnAxis(pos, false, bounds.x(), bounds.width()) && isInBoundsOnAxis(pos, true, bounds.y(), bounds.height());
}

void AnimationTextObject::setVideoControlPositioning(QObject *propertyHolder, QRectF rect, int videoBarHeight, int underVideoBarHeight,
                                                     int cacheX, int loopModeX, int frameTraversalX, int speedX) {
	int videoControlsHeight = videoBarHeight + underVideoBarHeight;
	QSizeF sizeMin = QSizeF(rect.width() - videoControlsHeight, rect.height() - videoControlsHeight);
	int xWithWidth = rect.x() + sizeMin.width();
	auto xOffset = [rect, xWithWidth](int offset) {
		return offset < 0 ? xWithWidth + offset : rect.x() + offset;
	};
	cacheX = xOffset(cacheX);
	loopModeX = xOffset(loopModeX);
	frameTraversalX = xOffset(frameTraversalX);
	speedX = xOffset(speedX);

	QList< QString > propertyNames = {
		"posAndSize",
		"videoBarHeight",
		"underVideoBarHeight",
		"cacheX",
		"loopModeX",
		"frameTraversalX",
		"speedX"
	};
	QList< QVariant > properties = {
		QVariant(rect),
		QVariant(videoBarHeight),
		QVariant(underVideoBarHeight),
		QVariant(cacheX),
		QVariant(loopModeX),
		QVariant(frameTraversalX),
		QVariant(speedX),
	};
	// Make the locations of certain interactive areas available through the object:
	for (int i = 0; i < properties.length(); ++i) {
		propertyHolder->setProperty(propertyNames[i].toStdString().c_str(), properties[i]);
	}
}

void AnimationTextObject::drawVideoControls(QPainter *painter, QObject *propertyHolder, QPixmap frame,
                                            bool isPaused, bool isCached, int frameIndex, int speed) {
	QRectF rect             = propertyHolder->property("posAndSize").toRectF();
	int lastFrameIndex      = propertyHolder->property("lastFrameIndex").toInt();
	int videoBarHeight      = propertyHolder->property("videoBarHeight").toInt();
	int underVideoBarHeight = propertyHolder->property("underVideoBarHeight").toInt();
	int cacheX              = propertyHolder->property("cacheX").toInt();
	int loopModeX           = propertyHolder->property("loopModeX").toInt();
	int frameTraversalX     = propertyHolder->property("frameTraversalX").toInt();
	int speedX              = propertyHolder->property("speedX").toInt();

	int videoControlsHeight = videoBarHeight + underVideoBarHeight;
	int videoBarTopLeftX = rect.x();
	int videoBarTopLeftY = rect.y() + rect.height() - videoControlsHeight;
	int underVideoBarY = videoBarTopLeftY + videoBarHeight;
	int underVideoBarWithMarginY = underVideoBarY + 14;
	QSizeF sizeMin = QSizeF(rect.width() - videoControlsHeight, rect.height() - videoControlsHeight);
	auto convertUnit = [](int integer, int exponent, int decimalAmount = 0) {
		bool noDecimals = decimalAmount == 0;
		int exponentForDecimalAmount = exponent < 0 ? exponent + decimalAmount : exponent - decimalAmount;

		double product = integer * pow(10, exponentForDecimalAmount);
		return noDecimals ? product : round(product) / (double)pow(10, decimalAmount);
	};
	auto padDecimals = [](QString numberStr, int decimalAmount) {
		int decimalMarkIndex = numberStr.lastIndexOf('.');
		bool isDecimal = decimalMarkIndex != -1 && decimalMarkIndex < numberStr.length() - 1;
		int currentDecimalAmount = isDecimal ? numberStr.sliced(decimalMarkIndex + 1).length() : 0;
		int decimalFillerAmount = decimalAmount - currentDecimalAmount;

		QString decimalFillers = QString('0').repeated(decimalFillerAmount);
		return decimalFillerAmount > 0 ? numberStr.append((decimalFillerAmount == 1 ? "." : "") + decimalFillers) : numberStr;
	};
	auto padNumber = [](QString numberStr, int digitAmount) {
		int numberLength = numberStr.length();
		int decimalMarkIndex = numberStr.lastIndexOf('.');
		int decimalsIncludingMarkLength = decimalMarkIndex != -1 ? numberLength - decimalMarkIndex : 0;
		return numberStr.rightJustified(digitAmount + decimalsIncludingMarkLength, '0');
	};
	auto formatTime = [padDecimals, padNumber](double seconds, double totalSeconds = 0) {
		auto getTimeNumbers = [](double seconds) {
			auto floorDivision = [](double dividend, double divisor = 60) {
				return floor(dividend / divisor);
			};
			int minutes = floorDivision(seconds);
			int hours = floorDivision(minutes);
			int remainingMinutes = std::max(minutes - hours * 60, 0);
			double remainingSeconds = std::max<double>(seconds - minutes * 60, 0);
			return QList< double >({ remainingSeconds, (double)remainingMinutes, (double)hours });
		};
		auto getDigitAmount = [](int number) {
			int digitAmount = 0;
			do {
				number /= 10;
				++digitAmount;
			} while (number != 0);
			return digitAmount;
		};

		QList< double > timeNumbers = getTimeNumbers(seconds);
		QList< double > totalTimeNumbers = totalSeconds == 0 ? timeNumbers : getTimeNumbers(totalSeconds);
		int timeNumberAmount = timeNumbers.length();
		int decimalAmount = 1;

		int lastTimeNumberIndex = 0;
		for (int i = timeNumberAmount - 1; i >= 0; --i) {
			if (totalTimeNumbers[i] > 0) {
				lastTimeNumberIndex = i;
				break;
			}
		}

		QString timeStr;
		for (int i = 0; i < timeNumberAmount; ++i) {
			double number = timeNumbers[i];
			bool isSeconds = i == 0;
			bool isLastNumber = i == lastTimeNumberIndex;
			bool hasAnotherNumber = i < lastTimeNumberIndex;
			if (number == 0 && !hasAnotherNumber && !isLastNumber) {
				break;
			}
			QString numberStr = QString::number(number);
			if (hasAnotherNumber || isLastNumber) {
				int digitAmount = isLastNumber ? getDigitAmount(totalTimeNumbers[i]) : 2;
				numberStr = padNumber(numberStr, digitAmount);
			}
			timeStr.prepend(isSeconds ? padDecimals(numberStr, decimalAmount) : numberStr.append(":"));
		}
		return timeStr;
	};

	QList< QVariant > frameDelays = propertyHolder->property("frameDelays").toList();
	int totalMs = propertyHolder->property("totalMs").toInt();
	int msUntilCurrentFrame = 0;
	bool isLastFrame = frameIndex == lastFrameIndex;
	// Determine the time until the current frame or the time until the end of the last frame
	// if on the last frame, so as to show a clear time for the start and end:
	for (int i = 0; i < (isLastFrame ? frameDelays.length() : frameIndex); ++i) {
		msUntilCurrentFrame += frameDelays[i].toInt();
	}
	// Convert to seconds rounded to one decimal:
	double totalS = convertUnit(totalMs, -3, 1);
	double currentS = convertUnit(msUntilCurrentFrame, -3, 1);

	painter->drawPixmap(QRect(rect.topLeft().toPoint(), sizeMin.toSize()), frame);
	painter->fillRect(videoBarTopLeftX, videoBarTopLeftY, sizeMin.width(), videoControlsHeight, QBrush(QColor(50, 50, 50, 180)));

	double videoBarProgress = msUntilCurrentFrame / (double)totalMs;
	QBrush videoBarBrush(QColor(0, 0, 200));
	painter->fillRect(videoBarTopLeftX, videoBarTopLeftY, sizeMin.width(), 4, QBrush(QColor(90, 90, 90, 180)));
	painter->fillRect(videoBarTopLeftX, videoBarTopLeftY, round(sizeMin.width() * videoBarProgress), 4, videoBarBrush);

	QString speedStr = padDecimals(QString::number(convertUnit(speed, -2)), 1);
	QPoint speedPos(speedX, underVideoBarWithMarginY);
	painter->drawText(speedPos, speedStr);
	// Draw the plus "+":
	painter->drawLine(speedPos.x() - 9, speedPos.y() - 11, speedPos.x() - 9, speedPos.y() - 3);
	painter->drawLine(speedPos.x() - 13, speedPos.y() - 7, speedPos.x() - 5, speedPos.y() - 7);
	// Draw the minus "-":
	painter->drawLine(speedPos.x() - 13, speedPos.y() + 2, speedPos.x() - 5, speedPos.y() + 2);
	// Draw the circle "o":
	painter->drawEllipse(speedPos.x() - 26, speedPos.y() - 6, 6, 6);

	QPoint frameTraversalPos(frameTraversalX, underVideoBarWithMarginY);
	painter->drawText(frameTraversalPos, "<  >");

	LoopMode loopMode = qvariant_cast< LoopMode >(propertyHolder->property("LoopMode"));
	QString loopModeStr = loopModeToString(loopMode);
	QFont font = painter->font();
	double fontSizeSmall = 0.7;
	font.setPointSize(font.pointSize() * fontSizeSmall);
	painter->setFont(font);
	painter->drawText(QPointF(loopModeX, underVideoBarY + 8), "mode:");
	painter->drawText(QPointF(loopModeX - (loopModeStr.length() > 6 ? 13 : 0), underVideoBarY + 17), loopModeStr);

	QString cachedStr = QString::fromStdString(isCached ? "on" : "off");
	painter->drawText(QPointF(cacheX, underVideoBarY + 8), "cache:");
	painter->drawText(QPointF(cacheX + 5, underVideoBarY + 17), cachedStr);
	font.setPointSize(font.pointSize() / fontSizeSmall);
	painter->setFont(font);

	QString totalTimeStr = formatTime(totalS);
	QString currentTimeStr = formatTime(currentS, totalS);
	painter->drawText(QPoint(videoBarTopLeftX + 20, underVideoBarWithMarginY), tr("%1 / %2").arg(currentTimeStr).arg(totalTimeStr));

	QPointF iconTopPos(videoBarTopLeftX + 2, underVideoBarY + 2);
	if (isPaused) {
		// Add a play-icon, which is a right-pointing triangle, like this "▶":
		QPolygonF polygon({
			iconTopPos,
			QPointF(videoBarTopLeftX + 15, underVideoBarY + 10),
			QPointF(videoBarTopLeftX + 2, underVideoBarY + 18)
		});
		QPainterPath path;
		path.addPolygon(polygon);
		painter->fillPath(path, QBrush(Qt::white));
	} else {
		// Add a pause-icon, which is two vertical rectangles next to each other, like this "||":
		QSize pauseBarSize(4, 16);
		QBrush brush(Qt::white);
		painter->fillRect(QRect(iconTopPos.toPoint(), pauseBarSize), brush);
		painter->fillRect(QRect(QPoint(videoBarTopLeftX + 11, underVideoBarY + 2), pauseBarSize), brush);
	}
}

AnimationTextObject::VideoController AnimationTextObject::mousePressVideoControls(QObject *propertyHolder, QPoint mouseDocPos) {
	QRectF rect             = propertyHolder->property("posAndSize").toRectF();
	int videoBarHeight      = propertyHolder->property("videoBarHeight").toInt();
	int underVideoBarHeight = propertyHolder->property("underVideoBarHeight").toInt();
	int cacheX              = propertyHolder->property("cacheX").toInt();
	int loopModeX           = propertyHolder->property("loopModeX").toInt();
	int frameTraversalX     = propertyHolder->property("frameTraversalX").toInt();
	int speedX              = propertyHolder->property("speedX").toInt();

	auto isThisInBoundsOnAxis = [mouseDocPos](bool yInsteadOfX, int start, int length) {
		return isInBoundsOnAxis(mouseDocPos, yInsteadOfX, start, length);
	};
	auto isThisInBounds = [mouseDocPos](QRectF bounds) {
		return isInBounds(mouseDocPos, bounds);
	};

	int videoControlsHeight = videoBarHeight + underVideoBarHeight;
	int videoControlsY = rect.y() + rect.height() - videoControlsHeight;
	int underVideoBarY = videoControlsY + videoBarHeight;
	int underVideoBarHalfHeight = underVideoBarHeight / 2;

	QRectF viewRect(rect.x(), rect.y(), rect.width() - videoControlsHeight, rect.height() - videoControlsHeight);
	QRectF playPauseRect(rect.x(), underVideoBarY, 15, underVideoBarHeight);

	QRectF cacheRect(cacheX, underVideoBarY, 25, underVideoBarHeight);
	QRectF loopModeRect(loopModeX, underVideoBarY, 24, underVideoBarHeight);

	int frameTraversalWidth = 12;
	QRectF framePreviousRect(frameTraversalX, underVideoBarY, frameTraversalWidth, underVideoBarHeight);
	QRectF frameNextRect(frameTraversalX + frameTraversalWidth + 2, underVideoBarY, frameTraversalWidth, underVideoBarHeight);

	int speedWidth = 9;
	QRectF speedResetRect(speedX - 28, underVideoBarY, speedWidth, underVideoBarHeight);
	QRectF speedMinusRect(speedX - 14, underVideoBarY + underVideoBarHalfHeight, speedWidth, underVideoBarHalfHeight);
	QRectF speedPlusRect(speedX - 14, underVideoBarY, speedWidth, underVideoBarHalfHeight);

	if (!isThisInBoundsOnAxis(false, viewRect.x(), viewRect.width())) return None;
	if (isThisInBoundsOnAxis(true, viewRect.y(), viewRect.height())) return View;
	if (isThisInBoundsOnAxis(true, videoControlsY, videoBarHeight)) return VideoBar;
	if (isThisInBounds(playPauseRect)) return PlayPause;
	if (isThisInBounds(cacheRect)) return CacheSwitch;
	if (isThisInBounds(loopModeRect)) return LoopSwitch;
	if (isThisInBounds(framePreviousRect)) return PreviousFrame;
	if (isThisInBounds(frameNextRect)) return NextFrame;
	if (isThisInBounds(speedResetRect)) return ResetSpeed;
	if (isThisInBounds(speedMinusRect)) return DecreaseSpeed;
	if (isThisInBounds(speedPlusRect)) return IncreaseSpeed;
	return None;
}

void AnimationTextObject::mousePress(QAbstractTextDocumentLayout *docLayout, QPoint mouseDocPos, Qt::MouseButton mouseButton) {
	QTextFormat baseFmt = docLayout->formatAt(mouseDocPos);
	if (!baseFmt.isCharFormat() || baseFmt.objectType() != Log::Animation) {
		return;
	}
	QMovie *animation = qvariant_cast< QMovie* >(baseFmt.toCharFormat().property(1));
	QRectF rect = animation->property("posAndSize").toRectF();
	int lastFrameIndex = animation->property("lastFrameIndex").toInt();
	int videoBarHeight = animation->property("videoBarHeight").toInt();
	int underVideoBarHeight = animation->property("underVideoBarHeight").toInt();
	int videoControlsHeight = videoBarHeight + underVideoBarHeight;
	int videoControlsY = rect.y() + rect.height() - videoControlsHeight;
	int widthMin = rect.width() - videoControlsHeight;

	QRectF videoControlsRect(rect.x(), videoControlsY, widthMin, videoControlsHeight);
	auto updateVideoControls = [videoControlsRect, docLayout]() {
		emit docLayout->update(videoControlsRect);
	};
	auto thisSetFrame = [animation](int frameIndex) {
		setFrame(animation, frameIndex);
	};
	auto setFrameByPercentage = [animation, thisSetFrame, lastFrameIndex](double percentage) {
		QList< QVariant > frameDelays = animation->property("frameDelays").toList();
		int totalMs = animation->property("totalMs").toInt();
		int msPassedAtSelection = round(percentage * totalMs);
		int msUntilCurrentFrame = 0;

		int frameIndex = 0;
		int frameDelayAmount = frameDelays.length();
		for (int i = 0; i < frameDelayAmount; ++i) {
			int delay = frameDelays[i].toInt();
			msUntilCurrentFrame += delay;
			if (msPassedAtSelection <= msUntilCurrentFrame) {
				bool isNextFrame = i + 1 < frameDelayAmount;
				int currentFrameDifference = msUntilCurrentFrame - msPassedAtSelection;
				int previousFrameDifference = currentFrameDifference - delay;
				int nextFrameDifference = isNextFrame ? msUntilCurrentFrame + frameDelays[i + 1].toInt() - msPassedAtSelection : -9999;

				bool isPreviousFrameCloser = abs(previousFrameDifference) < currentFrameDifference;
				bool isNextFrameCloser = isNextFrame ? abs(nextFrameDifference) < currentFrameDifference : false;
				// The first delay has passed by the second frame and so on,
				// hence the index is greater by 1 for the frame of the full delay:
				frameIndex = i + 1 + (isPreviousFrameCloser ? -1 : isNextFrameCloser ? 1 : 0);
				break;
			}
		}
		thisSetFrame(frameIndex);
	};
	auto setFrameByVideoBarSelection = [setFrameByPercentage, mouseDocPos, rect, widthMin]() {
		double videoBarPercentage = (mouseDocPos.x() - rect.x()) / widthMin;
		setFrameByPercentage(videoBarPercentage);
	};
	auto resetPlayback = [animation, thisSetFrame]() {
		// Show the first frame that the animation would continue from if started again,
		// indicating that the animation was stopped instead of paused:
		thisSetFrame(0);
		animation->stop();
	};
	auto togglePause = [animation]() {
		QMovie::MovieState state = animation->state();
		if (state == QMovie::NotRunning) {
			animation->start();
			// Ensure the animation starts on the first attempt to do so:
			animation->setPaused(false);
		} else {
			animation->setPaused(state != QMovie::Paused);
		}
	};
	auto toggleCache = [animation, thisSetFrame, updateVideoControls, lastFrameIndex]() {
		bool wasCached = animation->cacheMode() == QMovie::CacheAll;
		QMovie::CacheMode cacheModeToSet = wasCached ? QMovie::CacheNone : QMovie::CacheAll;
		QMovie::MovieState state = animation->state();
		bool wasPaused = state == QMovie::Paused;
		bool wasRunning = state == QMovie::Running;

		int previousFrame = animation->currentFrameNumber();
		// Turning caching on or off requires reloading the animation, which is done via `setDevice`,
		// otherwise it will not play properly or dispose of the cache when it is not to be used:
		animation->stop();
		QIODevice *device = animation->device();
		// Prepare the animation to be loaded when starting for the first time:
		device->reset();
		animation->setDevice(device);
		animation->setCacheMode(cacheModeToSet);
		animation->start();

		// Restore the state of the animation playback to what it was before reloading it:
		thisSetFrame(previousFrame);
		if (wasPaused || (!wasRunning && previousFrame != 0 && previousFrame != lastFrameIndex)) {
			animation->setPaused(true);
		} else if (!wasRunning) {
			animation->stop();
		}
		updateVideoControls();
	};
	auto setSpeed = [animation, updateVideoControls](int percentage) {
		// QMovie does not itself support reverse playback and
		// pausing the animation should only be done via the
		// paused state to avoid confusion:
		if (percentage > 0) {
			animation->setSpeed(percentage);
			updateVideoControls();
		}
	};
	auto changeLoopMode = [animation, updateVideoControls](int steps) {
		LoopMode loopMode = qvariant_cast< LoopMode >(animation->property("LoopMode"));
		int loopModeChangedTo = loopMode + steps;
		int loopModeResult = loopModeChangedTo > NoLoop ? 0 : loopModeChangedTo < 0 ? static_cast< int >(NoLoop) : loopModeChangedTo;
		animation->setProperty("LoopMode", static_cast< LoopMode >(loopModeResult));
		updateVideoControls();
	};
	auto changeFrame = [animation, thisSetFrame, lastFrameIndex](int amount) {
		int frameIndex = animation->currentFrameNumber() + amount;
		int amountOfTimesGreater = abs(floor(frameIndex / (double)lastFrameIndex));

		int lastFrameIndexScaledToInput = lastFrameIndex * amountOfTimesGreater;
		int frameIndexWrappedBackward = lastFrameIndexScaledToInput + 1 + frameIndex;
		int frameIndexWrappedForward = frameIndex - 1 - lastFrameIndexScaledToInput;
		thisSetFrame(frameIndex < 0 ? frameIndexWrappedBackward : frameIndex > lastFrameIndex ? frameIndexWrappedForward : frameIndex);
	};
	auto changeSpeed = [animation, setSpeed](int percentage) {
		setSpeed(animation->speed() + percentage);
	};

	bool isLeftMouseButtonPressed = mouseButton == Qt::LeftButton;
	bool isMiddleMouseButtonPressed = mouseButton == Qt::MiddleButton;
	if (areVideoControlsOn) {
		VideoController videoController = mousePressVideoControls(animation, mouseDocPos);
		if (isLeftMouseButtonPressed) {
			switch (videoController) {
				case VideoBar:      return setFrameByVideoBarSelection();
				case View:
				case PlayPause:     return togglePause();
				case CacheSwitch:   return toggleCache();
				case LoopSwitch:    return changeLoopMode(1);
				case PreviousFrame: return changeFrame(-1);
				case NextFrame:     return changeFrame(1);
				case ResetSpeed:    return setSpeed(100);
				case DecreaseSpeed: return changeSpeed(-10);
				case IncreaseSpeed: return changeSpeed(10);
			}
		} else if (isMiddleMouseButtonPressed) {
			switch (videoController) {
				case View:
				case PlayPause:     return resetPlayback();
				case LoopSwitch:    return changeLoopMode(-1);
				case PreviousFrame: return changeFrame(-5);
				case NextFrame:     return changeFrame(5);
				case DecreaseSpeed: return changeSpeed(-50);
				case IncreaseSpeed: return changeSpeed(50);
			}
		}
		return;
	}
	if (isLeftMouseButtonPressed) {
		togglePause();
	} else if (isMiddleMouseButtonPressed) {
		resetPlayback();
	}
	// Right mouse button shows the context menu for the text object,
	// which is handled where the custom context menu for the log is.
}

AnimationTextObject::AnimationTextObject() : QObject() {
}

QSizeF AnimationTextObject::intrinsicSize(QTextDocument *, int, const QTextFormat &fmt) {
	QMovie *animation = qvariant_cast< QMovie* >(fmt.property(1));
    return QSizeF(animation->currentPixmap().size());
}

void AnimationTextObject::drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int, const QTextFormat &fmt) {
	QMovie *animation = qvariant_cast< QMovie* >(fmt.property(1));
	QPixmap frame = animation->currentPixmap();
	bool isRunning = animation->state() == QMovie::Running;
	bool isCached = animation->cacheMode() == QMovie::CacheAll;
	painter->setRenderHint(QPainter::Antialiasing);

	// Set up how the animation updates, loop modes and the positional data for the video controls:
	if (animation->property("isNoUpdateSetup").toBool()) {
		int lastFrameIndex = animation->property("lastFrameIndex").toInt();
		auto refresh = [rect, doc]() {
			emit doc->documentLayout()->update(rect);
		};
		// Refresh the image on change:
		connect(animation, &QMovie::updated, this, refresh);
		// Ensure the image is refreshed once more when the animation is paused or stopped:
		connect(animation, &QMovie::stateChanged, this, [refresh](QMovie::MovieState currentState) {
			if (currentState != QMovie::Running) {
				refresh();
			}
		});
		// Start the animation again when it finishes if the loop mode is `Loop`:
		connect(animation, &QMovie::finished, this, [animation, lastFrameIndex]() {
			LoopMode loopMode = qvariant_cast< LoopMode >(animation->property("LoopMode"));
			if (loopMode == Loop) {
				animation->start();
			}
		});
		// Stop the animation at the end of the last frame if the loop mode is `NoLoop`:
		connect(animation, &QMovie::frameChanged, this, [animation, lastFrameIndex, this](int frameIndex) {
			LoopMode loopMode = qvariant_cast< LoopMode >(animation->property("LoopMode"));
			if (frameIndex != lastFrameIndex || loopMode != NoLoop || animation->state() == QMovie::Paused) {
				return;
			}
			QTimer::singleShot(animation->nextFrameDelay(), Qt::PreciseTimer, this, [animation, lastFrameIndex]() {
				LoopMode currentLoopMode = qvariant_cast< LoopMode >(animation->property("LoopMode"));
				if (currentLoopMode != NoLoop || animation->state() == QMovie::Paused) {
					return;
				}
				setFrame(animation, lastFrameIndex);
				animation->stop();
			});
		});
		animation->setProperty("isNoUpdateSetup", false);
		setVideoControlPositioning(animation, rect);
	}
	updatePropertyPositionIfChanged(animation, rect);

	if (areVideoControlsOn) {
		drawVideoControls(painter, animation, frame, !isRunning, isCached, animation->currentFrameNumber(), animation->speed());
		return;
	}
	painter->drawPixmap(rect.toRect(), frame);
	if (!isRunning) {
		drawCenteredPlayIcon(painter, rect);
	}
}


bool ChatbarTextEdit::event(QEvent *evt) {
	if (evt->type() == QEvent::ShortcutOverride) {
		return false;
	}

	if (evt->type() == QEvent::KeyPress) {
		QKeyEvent *kev = static_cast< QKeyEvent * >(evt);
		if ((kev->key() == Qt::Key_Enter || kev->key() == Qt::Key_Return) && !(kev->modifiers() & Qt::ShiftModifier)) {
			const QString msg = toPlainText();
			if (!msg.isEmpty()) {
				addToHistory(msg);
				if ((kev->modifiers() & Qt::ControlModifier) && !m_justPasted) {
					emit ctrlEnterPressed(msg);
				} else {
					emit entered(msg);
				}
				m_justPasted = false;
			}
			return true;
		}
		if (kev->key() == Qt::Key_Tab) {
			emit tabPressed();
			return true;
		} else if (kev->key() == Qt::Key_Backtab) {
			emit backtabPressed();
			return true;
		} else if (kev->key() == Qt::Key_Space && kev->modifiers() == Qt::ControlModifier) {
			emit ctrlSpacePressed();
			return true;
		} else if (kev->key() == Qt::Key_Up && kev->modifiers() == Qt::ControlModifier) {
			historyUp();
			return true;
		} else if (kev->key() == Qt::Key_Down && kev->modifiers() == Qt::ControlModifier) {
			historyDown();
			return true;
		} else if (kev->key() == Qt::Key_V && (kev->modifiers() & Qt::ControlModifier)) {
			if (kev->modifiers() & Qt::ShiftModifier) {
				pasteAndSend_triggered();
				return true;
			} else {
				// Remember that we just pasted into the chat field
				// and allow CTRL+Enter only when we are sure it was
				// released for at least one GUI cycle.
				// See #6568
				m_justPasted = true;
			}
		}
	}

	if (evt->type() == QEvent::KeyRelease) {
		QKeyEvent *kev = static_cast< QKeyEvent * >(evt);
		if (kev->key() == Qt::Key_Control) {
			m_justPasted = false;
		}
	}

	return QTextEdit::event(evt);
}

/**
 * The bar will try to complete the username, if the nickname
 * is already complete it will try to find a longer match. If
 * there is none it will cycle the nicknames alphabetically.
 * Nothing is done on mismatch.
 */
unsigned int ChatbarTextEdit::completeAtCursor() {
	// Get an alphabetically sorted list of usernames
	unsigned int id = 0;

	QList< QString > qlsUsernames;

	if (ClientUser::c_qmUsers.empty())
		return id;
	foreach (ClientUser *usr, ClientUser::c_qmUsers) { qlsUsernames.append(usr->qsName); }
	std::sort(qlsUsernames.begin(), qlsUsernames.end());

	QString target = QString();
	QTextCursor tc = textCursor();

	if (toPlainText().isEmpty() || tc.position() == 0) {
		target = qlsUsernames.first();
		tc.insertText(target);
	} else {
		bool bBaseIsName   = false;
		const int iend     = tc.position();
		const auto istart  = toPlainText().lastIndexOf(QLatin1Char(' '), iend - 1) + 1;
		const QString base = toPlainText().mid(istart, iend - istart);
		tc.setPosition(static_cast< int >(istart));
		tc.setPosition(iend, QTextCursor::KeepAnchor);

		if (qlsUsernames.last() == base) {
			bBaseIsName = true;
			target      = qlsUsernames.first();
		} else {
			if (qlsUsernames.contains(base)) {
				// Prevent to complete to what's already there
				while (qlsUsernames.takeFirst() != base) {
				}
				bBaseIsName = true;
			}

			foreach (QString name, qlsUsernames) {
				if (name.startsWith(base, Qt::CaseInsensitive)) {
					target = name;
					break;
				}
			}
		}

		if (bBaseIsName && target.isEmpty() && !qlsUsernames.empty()) {
			// If autocomplete failed and base was a name get the next one
			target = qlsUsernames.first();
		}

		if (!target.isEmpty()) {
			tc.insertText(target);
		}
	}

	if (!target.isEmpty()) {
		setTextCursor(tc);

		foreach (ClientUser *usr, ClientUser::c_qmUsers) {
			if (usr->qsName == target) {
				id = usr->uiSession;
				break;
			}
		}
	}
	return id;
}

void ChatbarTextEdit::addToHistory(const QString &str) {
	iHistoryIndex = -1;
	qslHistory.push_front(str);
	if (qslHistory.length() > MAX_HISTORY) {
		qslHistory.pop_back();
	}
}

void ChatbarTextEdit::historyUp() {
	if (qslHistory.length() == 0)
		return;

	if (iHistoryIndex == -1) {
		qsHistoryTemp = toPlainText();
	}

	if (iHistoryIndex < qslHistory.length() - 1) {
		setPlainText(qslHistory[++iHistoryIndex]);
		moveCursor(QTextCursor::End);
	}
}

void ChatbarTextEdit::historyDown() {
	if (iHistoryIndex < 0) {
		return;
	} else if (iHistoryIndex == 0) {
		setPlainText(qsHistoryTemp);
		iHistoryIndex--;
	} else {
		setPlainText(qslHistory[--iHistoryIndex]);
	}
	moveCursor(QTextCursor::End);
}

void ChatbarTextEdit::pasteAndSend_triggered() {
	paste();
	addToHistory(toPlainText());
	emit entered(toPlainText());
}

DockTitleBar::DockTitleBar() : QLabel(tr("Drag here")) {
	setAlignment(Qt::AlignCenter);
	setEnabled(false);
	qtTick = new QTimer(this);
	qtTick->setSingleShot(true);
	connect(qtTick, SIGNAL(timeout()), this, SLOT(tick()));
	size = newsize = 0;
}

QSize DockTitleBar::sizeHint() const {
	return minimumSizeHint();
}

QSize DockTitleBar::minimumSizeHint() const {
	return QSize(size, size);
}

bool DockTitleBar::eventFilter(QObject *, QEvent *evt) {
	QDockWidget *qdw = qobject_cast< QDockWidget * >(parentWidget());

	if (!this->isEnabled())
		return false;

	switch (evt->type()) {
		case QEvent::Leave:
		case QEvent::Enter:
		case QEvent::MouseMove:
		case QEvent::MouseButtonRelease: {
			newsize  = 0;
			QPoint p = qdw->mapFromGlobal(QCursor::pos());
			if ((p.x() >= static_cast< int >(static_cast< float >(qdw->width()) * 0.1f + 0.5f))
				&& (p.x() < static_cast< int >(static_cast< float >(qdw->width()) * 0.9f + 0.5f)) && (p.y() >= 0)
				&& (p.y() < 15))
				newsize = 15;
			if (newsize > 0 && !qtTick->isActive())
				qtTick->start(500);
			else if ((newsize == size) && qtTick->isActive())
				qtTick->stop();
			else if (newsize == 0)
				tick();
		}
		default:
			break;
	}

	return false;
}

void DockTitleBar::tick() {
	QDockWidget *qdw = qobject_cast< QDockWidget * >(parentWidget());

	if (newsize == size)
		return;

	size = newsize;
	qdw->setTitleBarWidget(this);
}
