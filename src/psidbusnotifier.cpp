/*
 * psidbusnotifier.cpp: Psi's interface to org.freedesktop.Notify
 * Copyright (C) 2012  Khryukin Evgeny
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * You can also redistribute and/or modify this program under the
 * terms of the Psi License, specified in the accompanied COPYING
 * file, as published by the Psi Project; either dated January 1st,
 * 2005, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusInterface>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QCoreApplication>
#include <QImage>

#include "psidbusnotifier.h"
#include "psiaccount.h"
#include "psievent.h"
#include "common.h"
#include "avatars.h"
#include "userlist.h"
#include "applicationinfo.h"
#include "psipopup.h"
#include "psioptions.h"
#include "psicon.h"


static int minLifeTime = 5000;

class iiibiiay
{
public:
	iiibiiay(QImage* img)
	{
		if(img->format() != QImage::Format_ARGB32)
			*img = img->convertToFormat(QImage::Format_ARGB32);
		width = img->width();
		height = img->height();
		rowstride = img->bytesPerLine();
		hasAlpha = img->hasAlphaChannel();
		channels = img->isGrayscale()?1:hasAlpha?4:3;
		bitsPerSample = img->depth()/channels;
		image.append((char*)img->rgbSwapped().bits(),img->numBytes());
	}
	iiibiiay(){}
	static const int id;
	int width;
	int height;
	int rowstride;
	bool hasAlpha;
	int bitsPerSample;
	int channels;
	QByteArray image;
};
Q_DECLARE_METATYPE(iiibiiay);

const int iiibiiay::id(qDBusRegisterMetaType<iiibiiay>());

QDBusArgument &operator<<(QDBusArgument &a, const iiibiiay &i)
{
	a.beginStructure();
	a << i.width << i.height << i.rowstride << i.hasAlpha << i.bitsPerSample << i.channels << i.image;
	a.endStructure();
	return a;
}
const QDBusArgument & operator >>(const QDBusArgument &a,  iiibiiay &i)
{
	a.beginStructure();
	a >> i.width >> i.height >> i.rowstride >> i.hasAlpha >> i.bitsPerSample >> i.channels >> i.image;
	a.endStructure();
	return a;
}

static QDBusMessage createMessage(const QString& method)
{
	return QDBusMessage::createMethodCall("org.freedesktop.Notifications",
					     "/org/freedesktop/Notifications",
					    "org.freedesktop.Notifications",
					    method);
}


PsiDBusNotifier::PsiDBusNotifier()
	: QObject(QCoreApplication::instance())
	, id_(0)
	, account_(0)
	, event_(0)
	, lifeTimer_(new QTimer(this))
{
	QDBusConnection::sessionBus().connect("org.freedesktop.Notifications",
					      "/org/freedesktop/Notifications",
					      "org.freedesktop.Notifications",
					      "NotificationClosed", this, SLOT(popupClosed(uint,uint)));
	lifeTimer_->setSingleShot(true);
	connect(lifeTimer_, SIGNAL(timeout()), SLOT(readyToDie()));
}

PsiDBusNotifier::~PsiDBusNotifier()
{
}

bool PsiDBusNotifier::isAvailable()
{
	static bool ret = QDBusInterface("org.freedesktop.Notifications",
					   "/org/freedesktop/Notifications",
					   "org.freedesktop.Notifications",
					   QDBusConnection::sessionBus()).isValid();
	return ret;
}

void PsiDBusNotifier::popup(PsiAccount* account, PsiPopup::PopupType type, const Jid& jid, const Resource& r, const UserListItem* uli, PsiEvent* event)
{
	account_ = account;
	jid_ = jid;
	if(event) {
		event_ = event;
		connect(event_, SIGNAL(destroyed()), SLOT(eventDestroyed()));
	}

	QString title, desc, contact, text, statusMsg;
	QString statusTxt = status2txt(makeSTATUS(r.status()));
	int len = PsiOptions::instance()->getOption("options.ui.notifications.passive-popups.maximum-status-length").toInt();
	if (len != 0)
		statusMsg = r.status().status();
	if (len > 0)
		if (((int)statusMsg.length()) > len)
			statusMsg = statusMsg.left(len) + "...";
	bool doAlert = false;
	PsiIcon* ico = 0;

	if (uli) {
		contact = uli->name();
	}
	else if (event && event->type() == PsiEvent::Auth) {
		contact = ((AuthEvent*) event)->nick();
	}
	else if (event && event->type() == PsiEvent::Message) {
		contact = ((MessageEvent*) event)->nick();
	}

	if (contact.isEmpty())
		contact = jid.bare();

	int jidLen = PsiOptions::instance()->getOption("options.ui.notifications.passive-popups.maximum-jid-length").toInt();
	if (jidLen > 0 && ((int)contact.length()) > jidLen)
		contact = contact.left(jidLen) + "...";

	text = contact;

	title = PsiPopup::title(type, &doAlert, &ico);

	QVariantMap hints;
	if(PsiOptions::instance()->getOption("options.ui.notifications.passive-popups.dbus.transient-hint").toBool() ||
	   type == PsiPopup::AlertComposing || type == PsiPopup::AlertOffline ||
	   type == PsiPopup::AlertOnline || type == PsiPopup::AlertStatusChange || type == PsiPopup::AlertNone)
	{
		hints.insert("transient", QVariant(true));
	}
//	if(doAlert) {
//		hints.insert("urgency", QVariant(2));
//	}
	QImage im;
	if(account) {
		im = account->avatarFactory()->getAvatar(jid.bare()).toImage();
		if(!im.isNull()) {
			int size = PsiOptions::instance()->getOption("options.ui.notifications.passive-popups.avatar-size").toInt();
			im = im.scaledToWidth(size, Qt::SmoothTransformation);
		}
	}

	if(im.isNull() && ico) {
		im = ico->pixmap().toImage();
	}

	if(!im.isNull()) {
		iiibiiay i(&im);
		hints.insert("icon_data", QVariant(iiibiiay::id, &i));
	}

	bool showMessage = PsiOptions::instance()->getOption("options.ui.notifications.passive-popups.showMessage").toBool();

	switch(type) {
	case PsiPopup::AlertOnline:
		text = QString("%1 (%2)").arg(contact).arg(statusTxt);
		desc = statusMsg;
		break;
	case PsiPopup::AlertOffline:
		text = QString("%1 (%2)").arg(contact).arg(statusTxt);
		desc = statusMsg;
		break;
	case PsiPopup::AlertStatusChange:
		text = QString("%1 (%2)").arg(contact).arg(statusTxt);
		desc = statusMsg;
		break;
	case PsiPopup::AlertComposing:
		text = QString("%1%2").arg(contact).arg(QObject::tr(" is typing..."));
		desc = "";
		break;
	case PsiPopup::AlertMessage: {
			text = QObject::tr("%1 says:").arg(contact);
			if(showMessage) {
				const Message* jmessage = &((MessageEvent *)event)->message();
				desc = jmessage->body();
			}
			break;
		}
	case PsiPopup::AlertChat: {
			if(showMessage) {
				const Message* jmessage = &((MessageEvent *)event)->message();
				desc = jmessage->body();
			}
			break;
		}
	case PsiPopup::AlertHeadline: {
			if(showMessage) {
				const Message* jmessage = &((MessageEvent *)event)->message();
				if ( !jmessage->subject().isEmpty())
					title = jmessage->subject();
				desc = jmessage->body();
			}
			break;
		}
	case PsiPopup::AlertFile:
		desc = QObject::tr("[Incoming File]");
		break;
	default:
		break;
	}

	if(!desc.isEmpty()) {
		desc = PsiPopup::clipText(desc);
		text += "\n" + desc;
	}

	int lifeTime = PsiPopup::timeout(type);
	QDBusMessage m = createMessage("Notify");
	QVariantList args;
	args << QString(ApplicationInfo::name());
	args << QVariant(QVariant::UInt);
	args << QVariant("");
	args << QString(title);
	args << QString(text);
	args << QStringList();
	args << hints;
	args << lifeTime;
	m.setArguments(args);
	QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(m);
	QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);

	connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
			 this, SLOT(asyncCallFinished(QDBusPendingCallWatcher*)));

	lifeTime = (lifeTime < 0) ? lifeTime : qMax(minLifeTime, lifeTime);
	if(lifeTime >= 0)
		lifeTimer_->start(lifeTime);
}

void PsiDBusNotifier::popup(PsiAccount *account, const Jid &jid, const PsiIcon *titleIcon, const QString &titleText,
			    const QPixmap *avatar, const PsiIcon */*icon*/, const QString &text)
{
	account_ = account;
	jid_ = jid;

	QVariantMap hints;
	if(PsiOptions::instance()->getOption("options.ui.notifications.passive-popups.dbus.transient-hint").toBool()) {
		hints.insert("transient", QVariant(true));
	}
	if(avatar || titleIcon) {
		int size = PsiOptions::instance()->getOption("options.ui.notifications.passive-popups.avatar-size").toInt();
		QImage im = avatar ? avatar->toImage().scaledToWidth(size, Qt::SmoothTransformation) : titleIcon->pixmap().toImage();
		iiibiiay i(&im);
		hints.insert("icon_data", QVariant(iiibiiay::id, &i));
	}

	int lifeTime = PsiPopup::timeout(PsiPopup::AlertStatusChange);
	QDBusMessage m = createMessage("Notify");
	QVariantList args;
	args << QString(ApplicationInfo::name());
	args << QVariant(QVariant::UInt);
	args << QVariant("");
	args << QString(titleText);
	args << QString(text);
	args << QStringList();
	args << hints;
	args << lifeTime;
	m.setArguments(args);
	QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(m);
	QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);

	connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
			 this, SLOT(asyncCallFinished(QDBusPendingCallWatcher*)));

	lifeTime = (lifeTime < 0) ? lifeTime : qMax(minLifeTime, lifeTime);
	if(lifeTime >= 0)
		lifeTimer_->start(lifeTime);
}

void PsiDBusNotifier::asyncCallFinished(QDBusPendingCallWatcher *watcher)
{
	QDBusPendingReply<uint> repl(*watcher);
	if(repl.value() == 0) {
		readyToDie();
	}
	else {
		id_ = repl.value();
	}
}

void PsiDBusNotifier::popupClosed(uint id, uint reason)
{
	if(id_ != 0 && id_ == id) {
		if(reason == 2) {
			if(account_) {
				if(event_) {
					account_->psi()->processEvent(event_, UserAction);
				}
				else if(jid_.isValid()) {
					account_->actionDefault(Jid(jid_.bare()));
				}
			}
		}
		readyToDie();
	}
}

void PsiDBusNotifier::eventDestroyed()
{
	event_ = 0;
}

void PsiDBusNotifier::readyToDie()
{
	if(lifeTimer_->isActive()) {
		lifeTimer_->stop();
	}

	QDBusConnection::sessionBus().disconnect("org.freedesktop.Notifications",
						 "/org/freedesktop/Notifications",
						 "org.freedesktop.Notifications",
						 "NotificationClosed", this, SLOT(popupClosed(uint,uint)));

	if(event_) {
		disconnect(event_, SIGNAL(destroyed()), this, SLOT(eventDestroyed()));
	}

	deleteLater();
}
