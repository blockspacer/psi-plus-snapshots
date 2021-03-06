/*
 * jignle-ft.h - Jingle file transfer
 * Copyright (C) 2019  Sergey Ilinykh
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "jingle-ft.h"
#include "jingle-nstransportslist.h"
#include "jingle-session.h"

#include "xmpp_client.h"
#include "xmpp_hash.h"
#include "xmpp_thumbs.h"
#include "xmpp_xmlcommon.h"

#include <cmath>
#include <functional>

namespace XMPP { namespace Jingle { namespace FileTransfer {

    const QString NS            = QStringLiteral("urn:xmpp:jingle:apps:file-transfer:5");
    const QString AMPLITUDES_NS = QStringLiteral("urn:audio:amplitudes");

    // tags
    static const QString AMPLITUDES_TAG = QStringLiteral("amplitudes");
    static const QString FILETAG        = QStringLiteral("file");
    static const QString DATE_TAG       = QStringLiteral("date");
    static const QString DESC_TAG       = QStringLiteral("desc");
    static const QString MEDIA_TYPE_TAG = QStringLiteral("media-type");
    static const QString NAME_TAG       = QStringLiteral("name");
    static const QString SIZE_TAG       = QStringLiteral("size");
    static const QString RANGE_TAG      = QStringLiteral("range");
    static const QString THUMBNAIL_TAG  = QStringLiteral("thumbnail");

    QDomElement Range::toXml(QDomDocument *doc) const
    {
        auto r = doc->createElement(RANGE_TAG);
        if (length) {
            r.setAttribute(QStringLiteral("length"), QString::number(length));
        }
        if (offset) {
            r.setAttribute(QStringLiteral("offset"), QString::number(offset));
        }
        for (auto const &h : hashes) {
            auto hel = h.toXml(doc);
            if (!hel.isNull()) {
                r.appendChild(hel);
            }
        }
        return r;
    }

    //----------------------------------------------------------------------------
    // File
    //----------------------------------------------------------------------------
    class File::Private : public QSharedData {
    public:
        bool        rangeSupported = false;
        bool        hasSize        = false;
        QDateTime   date;
        QString     mediaType;
        QString     name;
        QString     desc;
        qint64      size = 0;
        Range       range;
        QList<Hash> hashes;
        Thumbnail   thumbnail;
        QByteArray  amplitudes;
    };

    File::File() {}

    File::~File() {}

    File &File::operator=(const File &other)
    {
        d = other.d;
        return *this;
    }

    File::File(const File &other) : d(other.d) {}

    File::File(const QDomElement &file)
    {
        QDateTime   date;
        QString     mediaType;
        QString     name;
        QString     desc;
        qint64      size           = 0;
        bool        rangeSupported = false;
        bool        hasSize        = false;
        Range       range;
        QList<Hash> hashes;
        Thumbnail   thumbnail;
        QByteArray  amplitudes;

        bool ok;

        for (QDomElement ce = file.firstChildElement(); !ce.isNull(); ce = ce.nextSiblingElement()) {

            if (ce.tagName() == DATE_TAG) {
                date = QDateTime::fromString(ce.text().left(19), Qt::ISODate);
                if (!date.isValid()) {
                    return;
                }

            } else if (ce.tagName() == MEDIA_TYPE_TAG) {
                mediaType = ce.text();

            } else if (ce.tagName() == NAME_TAG) {
                name = ce.text();

            } else if (ce.tagName() == SIZE_TAG) {
                size = ce.text().toLongLong(&ok);
                if (!ok || size < 0) {
                    return;
                }
                hasSize = true;

            } else if (ce.tagName() == RANGE_TAG) {
                if (ce.hasAttribute(QLatin1String("offset"))) {
                    range.offset = ce.attribute(QLatin1String("offset")).toLongLong(&ok);
                    if (!ok || range.offset < 0) {
                        return;
                    }
                }
                if (ce.hasAttribute(QLatin1String("length"))) {
                    range.length = ce.attribute(QLatin1String("length")).toLongLong(&ok);
                    if (!ok || range.length <= 0) { // length should absent if we need to read till end of file.
                                                    // 0-length is nonsense
                        return;
                    }
                }
                QDomElement hashEl = ce.firstChildElement(QLatin1String("hash"));
                for (; !hashEl.isNull(); hashEl = hashEl.nextSiblingElement(QLatin1String("hash"))) {
                    if (hashEl.namespaceURI() == HASH_NS) {
                        auto hash = Hash(hashEl);
                        if (hash.type() == Hash::Type::Unknown) {
                            continue;
                        }
                        range.hashes.append(hash);
                    }
                }
                rangeSupported = true;

            } else if (ce.tagName() == DESC_TAG) {
                desc = ce.text();

            } else if (ce.tagName() == QLatin1String("hash")) {
                if (ce.namespaceURI() == HASH_NS) {
                    Hash h(ce);
                    if (h.type() == Hash::Type::Unknown) {
                        return;
                    }
                    hashes.append(h);
                }

            } else if (ce.tagName() == QLatin1String("hash-used")) {
                if (ce.namespaceURI() == HASH_NS) {
                    Hash h(ce);
                    if (h.type() == Hash::Type::Unknown) {
                        return;
                    }
                    hashes.append(h);
                }

            } else if (ce.tagName() == THUMBNAIL_TAG) {
                thumbnail = Thumbnail(ce);
            } else if (ce.tagName() == AMPLITUDES_TAG && ce.namespaceURI() == AMPLITUDES_NS) {
                amplitudes = QByteArray::fromBase64(ce.text().toLatin1());
            }
        }

        auto p            = new Private;
        p->date           = date;
        p->mediaType      = mediaType;
        p->name           = name;
        p->desc           = desc;
        p->size           = size;
        p->rangeSupported = rangeSupported;
        p->hasSize        = hasSize;
        p->range          = range;
        p->hashes         = hashes;
        p->thumbnail      = thumbnail;
        p->amplitudes     = amplitudes;

        d = p;
    }

    QDomElement File::toXml(QDomDocument *doc) const
    {
        if (!isValid() || d->hashes.isEmpty()) {
            return QDomElement();
        }
        QDomElement el = doc->createElementNS(NS, QStringLiteral("file"));
        if (d->date.isValid()) {
            el.appendChild(XMLHelper::textTag(*doc, DATE_TAG, d->date.toString(Qt::ISODate)));
        }
        if (d->desc.size()) {
            el.appendChild(XMLHelper::textTag(*doc, DESC_TAG, d->desc));
        }
        for (const auto &h : d->hashes) {
            el.appendChild(h.toXml(doc));
        }
        if (d->mediaType.size()) {
            el.appendChild(XMLHelper::textTag(*doc, MEDIA_TYPE_TAG, d->mediaType));
        }
        if (d->name.size()) {
            el.appendChild(XMLHelper::textTag(*doc, NAME_TAG, d->name));
        }
        if (d->hasSize) {
            el.appendChild(XMLHelper::textTag(*doc, SIZE_TAG, d->size));
        }
        if (d->rangeSupported || d->range.isValid()) {
            el.appendChild(d->range.toXml(doc));
        }
        if (d->thumbnail.isValid()) {
            el.appendChild(d->thumbnail.toXml(doc));
        }
        if (d->amplitudes.size()) {
            el.appendChild(XMLHelper::textTagNS(doc, AMPLITUDES_NS, AMPLITUDES_TAG, d->amplitudes));
        }
        return el;
    }

    bool File::merge(const File &other)
    {
        if (!d->thumbnail.isValid()) {
            d->thumbnail = other.thumbnail();
        }
        for (auto const &h : other.d->hashes) {
            auto it = std::find_if(d->hashes.constBegin(), d->hashes.constEnd(),
                                   [&h](auto const &v) { return h.type() == v.type(); });
            if (it == d->hashes.constEnd()) {
                d->hashes.append(h);
            } else if (h.data() != it->data()) {
                return false; // hashes are different
            }
        }
        return true;
    }

    bool File::hasComputedHashes() const
    {
        if (!d)
            return false;
        for (auto const &h : d->hashes) {
            if (h.data().size())
                return true;
        }
        return false;
    }

    bool File::hasSize() const { return d->hasSize; }

    QDateTime File::date() const { return d ? d->date : QDateTime(); }

    QString File::description() const { return d ? d->desc : QString(); }

    QList<Hash> File::hashes() const { return d ? d->hashes : QList<Hash>(); }
    QList<Hash> File::computedHashes() const
    {
        QList<Hash> ret;
        if (!d)
            return ret;
        for (auto const &h : d->hashes) {
            if (h.data().size())
                ret.append(h);
        }
        return ret;
    }

    Hash File::hash(Hash::Type t) const
    {
        if (d && d->hashes.count()) {
            if (t == Hash::Unknown)
                return d->hashes.at(0);
            for (auto const &h : d->hashes) {
                if (h.type() == t) {
                    return h;
                }
            }
        }
        return Hash();
    }

    QString File::mediaType() const { return d ? d->mediaType : QString(); }

    QString File::name() const { return d ? d->name : QString(); }

    qint64 File::size() const { return d ? d->size : 0; }

    Range File::range() const { return d ? d->range : Range(); }

    Thumbnail File::thumbnail() const { return d ? d->thumbnail : Thumbnail(); }

    QByteArray File::amplitudes() const { return d ? d->amplitudes : QByteArray(); }

    void File::setDate(const QDateTime &date) { ensureD()->date = date; }

    void File::setDescription(const QString &desc) { ensureD()->desc = desc; }

    void File::addHash(const Hash &hash) { ensureD()->hashes.append(hash); }

    void File::setHashes(const QList<Hash> &hashes) { ensureD()->hashes = hashes; }

    void File::setMediaType(const QString &mediaType) { ensureD()->mediaType = mediaType; }

    void File::setName(const QString &name) { ensureD()->name = name; }

    void File::setSize(qint64 size)
    {
        ensureD()->size = size;
        d->hasSize      = true;
    }

    void File::setRange(const Range &range)
    {
        ensureD()->range  = range;
        d->rangeSupported = true;
    }

    void File::setThumbnail(const Thumbnail &thumb) { ensureD()->thumbnail = thumb; }

    void File::setAmplitudes(const QByteArray &amplitudes) { d->amplitudes = amplitudes; }

    File::Private *File::ensureD()
    {
        if (!d) {
            d = new Private;
        }
        return d.data();
    }

    //----------------------------------------------------------------------------
    // Checksum
    //----------------------------------------------------------------------------
    Checksum::Checksum(const QDomElement &cs) : ContentBase(cs)
    {
        file = File(cs.firstChildElement(QLatin1String("file")));
    }

    bool Checksum::isValid() const { return ContentBase::isValid() && file.isValid(); }

    QDomElement Checksum::toXml(QDomDocument *doc) const
    {
        auto el = ContentBase::toXml(doc, "checksum");
        if (!el.isNull()) {
            el.appendChild(file.toXml(doc));
        }
        return el;
    }

    //----------------------------------------------------------------------------
    // Received
    //----------------------------------------------------------------------------
    QDomElement Received::toXml(QDomDocument *doc) const { return ContentBase::toXml(doc, "received"); }

    //----------------------------------------------------------------------------
    // ApplicationManager
    //----------------------------------------------------------------------------
    Manager::Manager(QObject *parent) : XMPP::Jingle::ApplicationManager(parent) {}

    Manager::~Manager()
    {
        if (jingleManager)
            jingleManager->unregisterApp(NS);
    }

    void Manager::setJingleManager(XMPP::Jingle::Manager *jm) { jingleManager = jm; }

    Application *Manager::startApplication(const ApplicationManagerPad::Ptr &pad, const QString &contentName,
                                           Origin creator, Origin senders)
    {
        if (!(contentName.size() > 0 && (senders == Origin::Initiator || senders == Origin::Responder))) {
            qDebug("Invalid Jignle FT App start parameters");
            return nullptr;
        }
        return new Application(pad.staticCast<Pad>(), contentName, creator, senders); // ContentOrigin::Remote
    }

    ApplicationManagerPad *Manager::pad(Session *session) { return new Pad(this, session); }

    void Manager::closeAll() {}

    Client *Manager::client()
    {
        if (jingleManager) {
            return jingleManager->client();
        }
        return nullptr;
    }

    QStringList Manager::availableTransports() const
    {
        return jingleManager->availableTransports(TransportFeature::Reliable);
    }

    //----------------------------------------------------------------------------
    // Application
    //----------------------------------------------------------------------------
    class Application::Private {
    public:
        struct TransportDesc {
            Origin                    creator = Origin::None;
            State                     state   = State::Created;
            QSharedPointer<Transport> transport;
        };

        Application *q = nullptr;

        Reason updateReason;
        // Action              updateToSend            = Action::NoAction;
        bool                closeDeviceOnFinish = true;
        bool                streamingMode       = false;
        bool                endlessRange        = false; // where range in accepted file doesn't have end
        bool                outgoingReceived    = false;
        File                file;
        File                acceptFile; // as it comes with "accept" response
        XMPP::Stanza::Error lastError;
        Reason              lastReason;
        Connection::Ptr     connection;
        QIODevice *         device    = nullptr;
        qint64              bytesLeft = 0;
        QList<Hash>         outgoingChecksum;
        qint64              outgoingChecksumRangeOffset = 0, outgoingChecksumRangeLength = 0;

        void setState(State s)
        {
            q->_state = s;
            if (s == State::Finished) {
                if (device && closeDeviceOnFinish) {
                    device->close();
                }
                if (connection) {
                    connection->close();
                }
                q->disconnect(q->transport().data(), &Transport::updated, q, nullptr);
            }
            if (s >= State::Finishing) {
                q->disconnect(q->transport().data(), &Transport::failed, q, nullptr);
                q->disconnect(q->transport().data(), &Transport::connected, q, nullptr);
                // we can still try to send transport updates
            }
            emit q->stateChanged(s);
        }

        void handleStreamFail()
        {
            lastReason = Reason(Reason::Condition::FailedApplication, QString::fromLatin1("stream failed"));
            setState(State::Finished);
        }

        void writeNextBlockToTransport()
        {
            if (!(endlessRange || bytesLeft)) {
                lastReason = Reason(Reason::Condition::Success);
                setState(State::Finished);
                return; // everything is written
            }
            auto sz = qint64(connection->blockSize());
            sz      = sz ? sz : 8192;
            if (!endlessRange && sz > bytesLeft) {
                sz = bytesLeft;
            }
            QByteArray data;
            if (device->isSequential()) {
                if (!device->bytesAvailable())
                    return; // we will come back on readyRead
                data = device->read(qMin(qint64(sz), device->bytesAvailable()));
            } else {
                data = device->read(sz);
            }
            if (data.isEmpty()) {
                if (endlessRange) {
                    lastReason = Reason(Reason::Condition::Success);
                    setState(State::Finished);
                } else {
                    handleStreamFail();
                }
                return;
            }
            // qDebug("JINGLE-FT write %d bytes to connection", data.size());
            if (connection->write(data) == -1) {
                handleStreamFail();
                return;
            }
            emit q->progress(device->pos());
            bytesLeft -= data.size();
        }

        void readNextBlockFromTransport()
        {
            qint64 bytesAvail;
            while (bytesLeft && (bytesAvail = connection->bytesAvailable())) {
                qint64 sz = 65536; // shall we respect transport->blockSize() ?
                if (sz > bytesLeft) {
                    sz = bytesLeft;
                }
                if (sz > bytesAvail) {
                    sz = bytesAvail;
                }
                QByteArray data = connection->read(sz);
                // qDebug("JINGLE-FT read %d bytes from connection", data.size());
                if (data.isEmpty()) {
                    handleStreamFail();
                    return;
                }
                if (device->write(data) == -1) {
                    handleStreamFail();
                    return;
                }
                emit q->progress(device->pos());
                bytesLeft -= sz;
            }
            if (!bytesLeft) {
                // TODO send <received>
                lastReason = Reason(Reason::Condition::Success);
                setState(State::Finished);
            }
        }
    };

    Application::Application(const QSharedPointer<Pad> &pad, const QString &contentName, Origin creator,
                             Origin senders) :
        d(new Private)
    {
        d->q         = this;
        _pad         = pad;
        _contentName = contentName;
        _creator     = creator;
        _senders     = senders;
        _transportSelector.reset(
            new NSTransportsList(pad->session(), static_cast<Manager *>(pad->manager())->availableTransports()));
    }

    Application::~Application() {}

    void Application::setState(State state) { d->setState(state); }

    Stanza::Error Application::lastError() const { return d->lastError; }

    Reason Application::lastReason() const { return d->lastReason; }

    static Application::SetDescError parseDescription(const QDomElement &description, File &file)
    {
        auto el = description.firstChildElement("file");
        if (el.isNull())
            return Application::Unparsed;

        auto f = File(el);
        if (!f.isValid())
            return Application::IncompatibleParameters;

        file = f;
        return Application::Ok;
    }

    Application::SetDescError Application::setRemoteOffer(const QDomElement &description)
    {
        File f;
        auto ret = parseDescription(description, f);
        if (ret == Application::Ok)
            d->file = f;
        return ret;
    }

    Application::SetDescError Application::setRemoteAnswer(const QDomElement &description)
    {
        File f;
        auto ret = parseDescription(description, f);
        if (ret == Application::Ok) {
            d->acceptFile = f;
            setState(State::Accepted);
        }
        return ret;
    }

    void Application::prepareThumbnail(File &file)
    {
        if (file.thumbnail().data.size()) {
            auto    client = _pad->session()->manager()->client();
            auto    thumb  = file.thumbnail();
            auto    bm     = client->bobManager();
            BoBData data   = bm->append(thumb.data, thumb.mimeType);
            thumb.uri      = QLatin1String("cid:") + data.cid();
            d->file.setThumbnail(thumb);
        }
    }

    QDomElement Application::makeLocalOffer()
    {
        if (!d->file.isValid()) {
            return QDomElement();
        }
        auto doc = _pad->doc();
        auto el  = doc->createElementNS(NS, "description");

        prepareThumbnail(d->file);
        el.appendChild(d->file.toXml(doc));
        return el;
    }

    QDomElement Application::makeLocalAnswer()
    {
        if (!d->file.isValid()) {
            return QDomElement();
        }
        if (!d->acceptFile.isValid()) {
            d->acceptFile = d->file;
        }
        auto doc = _pad->doc();
        auto el  = doc->createElementNS(NS, "description");
        el.appendChild(d->acceptFile.toXml(doc));
        return el;
    }

    void Application::setFile(const File &file) { d->file = file; }

    File Application::file() const { return d->file; }

    File Application::acceptFile() const { return d->acceptFile; }

    bool Application::isTransportReplaceEnabled() const { return _state < State::Active; }

    void Application::initTransport()
    {
        connect(_transport.data(), &Transport::connected, this, [this]() {
            d->lastReason = Reason();
            d->lastError.reset();
            d->connection = _transport->addChannel();
            if (!d->streamingMode) {
                connect(d->connection.data(), &Connection::readyRead, this, [this]() {
                    if (!d->device) {
                        return;
                    }
                    if (_pad->session()->role() != _senders) {
                        d->readNextBlockFromTransport();
                    }
                });
                connect(d->connection.data(), &Connection::bytesWritten, this, [this](qint64 bytes) {
                    Q_UNUSED(bytes)
                    if (_pad->session()->role() == _senders && !d->connection->bytesToWrite()) {
                        d->writeNextBlockToTransport();
                    }
                });
            }

            d->setState(State::Active);
            if (!d->streamingMode) {
                if (d->acceptFile.range().isValid()) {
                    d->bytesLeft = d->acceptFile.range().length;
                    if (!d->bytesLeft)
                        d->endlessRange = true;
                    emit deviceRequested(d->acceptFile.range().offset, d->bytesLeft);
                } else {
                    d->bytesLeft = d->acceptFile.size();
                    emit deviceRequested(0, d->bytesLeft);
                }
            } else {
                emit connectionReady();
            }
        });
    }

    void Application::setStreamingMode(bool mode)
    {
        if (_state <= State::Connecting) {
            d->streamingMode = mode;
        }
    }

    XMPP::Jingle::Application::Update Application::evaluateOutgoingUpdate()
    {
        if (!isValid()) {
            _update = { Action::NoAction, Reason() };
            return _update;
        }

        if (_state == State::Active && (d->outgoingChecksum.size() > 0 || d->outgoingReceived))
            _update = { Action::SessionInfo, Reason() };
        else
            return XMPP::Jingle::Application::evaluateOutgoingUpdate();
        return _update;
    }

    OutgoingUpdate Application::takeOutgoingUpdate()
    {
        if (_update.action == Action::NoAction) {
            return OutgoingUpdate();
        }

        auto client = _pad->session()->manager()->client();
        auto doc    = client->doc();

        if (_update.action == Action::SessionInfo && (d->outgoingChecksum.size() > 0 || d->outgoingReceived)) {
            if (d->outgoingReceived) {
                d->outgoingReceived = false;
                ContentBase cb(_pad->session()->role(), _contentName);
                return OutgoingUpdate { QList<QDomElement>() << cb.toXml(doc, "received", NS),
                                        [this](bool) { d->setState(State::Finished); } };
            }
            if (!d->outgoingChecksum.isEmpty()) {
                ContentBase cb(_pad->session()->role(), _contentName);
                File        f;
                if (d->outgoingChecksumRangeOffset || d->outgoingChecksumRangeLength) {
                    Range r;
                    r.hashes = d->outgoingChecksum;
                    r.offset = d->outgoingChecksumRangeOffset;
                    r.length = d->outgoingChecksumRangeLength;
                    f.setRange(r);
                } else {
                    f.setHashes(d->outgoingChecksum);
                }
                auto el = cb.toXml(doc, "checksum", NS);
                el.appendChild(f.toXml(doc));
                d->outgoingChecksum.clear();
                return OutgoingUpdate { QList<QDomElement>() << el, [this](bool) { d->setState(State::Finished); } };
            }
        }
        if (_update.action == Action::ContentAdd && _creator == _pad->session()->role()) {
            // we are doing outgoing file transfer request. so need thumbnail
        }

        return XMPP::Jingle::Application::takeOutgoingUpdate();
    }

    void Application::prepare()
    {
        if (!_transport) {
            selectNextTransport();
        }
        if (_transport) {
            d->setState(State::ApprovedToSend);
            _transport->prepare();
        }
    }

    void Application::start()
    {
        if (_transport) {
            d->setState(State::Connecting);
            _transport->start();
        }
        // TODO we need QIODevice somewhere here
    }

    void Application::remove(Reason::Condition cond, const QString &comment)
    {
        if (_state >= State::Finishing)
            return;

        _terminationReason = Reason(cond, comment);
        _transport->disconnect(this);
        _transport.reset();

        if (_creator == _pad->session()->role() && _state <= State::ApprovedToSend) {
            // local content, not yet sent to remote
            setState(State::Finished);
            return;
        }

        emit updated();
    }

    void Application::incomingRemove(const Reason &r)
    {
        d->lastReason = r;
        d->setState(State::Finished);
    }

    bool Application::isValid() const
    {
        return d->file.isValid() && _contentName.size() > 0
            && (_senders == Origin::Initiator || _senders == Origin::Responder);
    }

    void Application::setDevice(QIODevice *dev, bool closeOnFinish)
    {
        if (!dev) { // failed to provide proper device
            _terminationReason
                = Reason(Reason::Condition::FailedApplication, QString::fromLatin1("No destination device"));
            emit updated();
            return;
        }
        d->device              = dev;
        d->closeDeviceOnFinish = closeOnFinish;
        if (_senders == _pad->session()->role()) {
            d->writeNextBlockToTransport();
        } else {
            d->readNextBlockFromTransport();
        }
    }

    Connection::Ptr Application::connection() const { return d->connection.staticCast<XMPP::Jingle::Connection>(); }

    Pad::Pad(Manager *manager, Session *session) : _manager(manager), _session(session) {}

    QDomElement Pad::takeOutgoingSessionInfoUpdate()
    {
        return QDomElement(); // TODO
    }

    QString Pad::ns() const { return NS; }

    Session *Pad::session() const { return _session; }

    ApplicationManager *Pad::manager() const { return _manager; }

    QString Pad::generateContentName(Origin senders)
    {
        QString prefix = senders == _session->role() ? "fileoffer" : "filereq";
        QString name;
        do {
            name = prefix + QString("_%1").arg(qrand() & 0xffff, 4, 16, QChar('0'));
        } while (_session->content(name, _session->role()));
        return name;
    }

    void Pad::addOutgoingOffer(const File &file)
    {
        auto selfp = _session->applicationPad(NS);
        auto app   = _manager->startApplication(selfp, "ft", _session->role(), _session->role());
        app->setFile(file);
    }

} // namespace FileTransfer
} // namespace Jingle
} // namespace XMPP
