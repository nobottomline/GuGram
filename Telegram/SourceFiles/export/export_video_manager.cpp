#include "export/export_video_manager.h"

#include "export/export_video_files.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "history/history.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "storage/file_download_mtproto.h"
#include "storage/storage_account.h"
#include "mtproto/sender.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "ui/style/style_core_palette.h"
#include "base/power_save_blocker.h"

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QLockFile>
#include <QtCore/QStorageInfo>
#include <QtCore/QThreadPool>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtCore/QSet>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

namespace Export {
namespace {

constexpr auto kParallelDownloads = 3;
constexpr auto kParallelScans = 2;
constexpr auto kPageSize = 100;
constexpr auto kMaxAttempts = 3;

QString Bytes(qint64 value) {
	return QString::number(value / (1024. * 1024.), 'f', 1) + u" MB"_q;
}

using State = VideoExportState;

QString VideoStateText(State state) {
	switch (state) {
	case State::None: return QString();
	case State::Preparing: return tr::lng_video_export_preparing(tr::now);
	case State::Scanning: return tr::lng_video_export_scanning(tr::now);
	case State::Downloading: return tr::lng_video_export_downloading(tr::now);
	case State::Paused: return tr::lng_video_export_paused(tr::now);
	case State::Cancelled: return tr::lng_video_export_cancelled(tr::now);
	case State::Complete: return tr::lng_video_export_complete(tr::now);
	case State::Failed: return tr::lng_video_export_failed(tr::now);
	}
	Unexpected("Video export state");
}

QString StateKey(State state) {
	switch (state) {
	case State::None: return u"none"_q;
	case State::Preparing: return u"preparing"_q;
	case State::Scanning: return u"scanning"_q;
	case State::Downloading: return u"downloading"_q;
	case State::Paused: return u"paused"_q;
	case State::Cancelled: return u"cancelled"_q;
	case State::Complete: return u"complete"_q;
	case State::Failed: return u"incomplete"_q;
	}
	Unexpected("Video export state");
}

} // namespace

class VideoManager::Impl final : public base::has_weak_ptr {
public:
	explicit Impl(not_null<Main::Session*> session);
	~Impl();
	void chooseFolder(not_null<PeerData*> peer);
	void show();
	[[nodiscard]] State state(not_null<PeerData*> peer) const;

private:
	struct Video {
		VideoFiles::Entry entry;
		not_null<DocumentData*> document;
		FullMsgId origin;
		int attempts = 0;
		bool requiresBypass = false;
	};
	struct Job {
		explicit Job(not_null<PeerData*> value) : peer(value) {
		}

		not_null<PeerData*> peer;
		QString parent;
		QString folder;
		VideoFiles::Manifest manifest;
		std::vector<Video> videos;
		QSet<QString> discovered;
		std::vector<not_null<PeerData*>> sources;
		int source = 0;
		int offset = 0;
		int snapshot = 0;
		int scanned = 0;
		int scanAttempts = 0;
		mtpRequestId request = 0;
		qint64 waitUntil = 0;
		State state = State::Preparing;
		QString error;
		std::unique_ptr<QLockFile> lock;
		std::unique_ptr<mtpFileLoader> loader;
		std::shared_ptr<std::atomic<bool>> cancelled;
		int current = -1;
		int generation = 0;
		bool ioPending = false;
		bool needsSave = false;
		qint64 speedBytes = 0;
		qint64 speedTime = 0;
		double speed = 0.;
		rpl::lifetime loading;
	};

	void add(not_null<PeerData*> peer, const QString &parent);
	void prepare(Job &job);
	void scan(Job &job);
	void receive(Job &job, const MTPmessages_Messages &result);
	void media(
		Job &job,
		FullMsgId origin,
		const MTPMessageMedia &media,
		bool requiresBypass);
	void pump();
	void download(Job &job, int index);
	void finish(Job &job, int generation, QString error, bool publish);
	void save(Job &job);
	void stop(Job &job, State state);
	void resume(Job &job);
	void refresh();
	void refreshStates();
	void applyStates(const VideoExportStates &states, bool overwrite);
	Job *selected();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	QThreadPool _workers;
	QElapsedTimer _clock;
	QTimer _timer;
	std::unique_ptr<base::PowerSaveBlocker> _keepAwake;
	std::unique_ptr<QDialog> _panel;
	QTableWidget *_table = nullptr;
	QLabel *_summary = nullptr;
	QPushButton *_pause = nullptr;
	QPushButton *_cancel = nullptr;
	QPushButton *_retry = nullptr;
	std::vector<std::unique_ptr<Job>> _jobs;
	QMap<DocumentId, QString> _completed;
	QSet<DocumentId> _activeDocuments;
	VideoExportStates _states;
	std::shared_ptr<std::atomic<bool>> _restoreCancelled = std::make_shared<std::atomic<bool>>(false);
	bool _restoreAnnounced = false;
	int _roundRobin = 0;

};

VideoManager::Impl::Impl(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp()) {
	_states = RestoreVideoExportStates(
		QString::number(session->uniqueId()),
		session->local().readPref<QString>("video-export-statuses"));
	_workers.setMaxThreadCount(kParallelDownloads);
	const auto parent = session->local().readPref<QString>("video-export-parent");
	if (!parent.isEmpty()) {
		const auto account = QString::number(session->uniqueId());
		const auto cancelled = _restoreCancelled;
		_workers.start([=, weak = base::make_weak(this)] {
			const auto states = ImportVideoExportStates(parent, account, *cancelled);
			crl::on_main(weak, [=] { applyStates(states, false); });
		});
	}
	_clock.start();
	QObject::connect(&_timer, &QTimer::timeout, [=] {
		pump();
		refresh();
	});
	_timer.start(250);
}

VideoManager::Impl::~Impl() {
	_timer.stop();
	_restoreCancelled->store(true);
	_panel = nullptr;
	for (const auto &job : _jobs) {
		job->cancelled->store(true);
		if (job->request) {
			_api.request(base::take(job->request)).cancel();
		}
		job->loading.destroy();
		job->loader = nullptr;
	}
	_workers.waitForDone();
}

void VideoManager::Impl::chooseFolder(not_null<PeerData*> peer) {
	show();
	FileDialog::GetFolder(
		_panel.get(),
		tr::lng_video_export_choose_folder(tr::now),
		_session->local().readPref<QString>("video-export-parent"),
		crl::guard(this, [=](QString &&folder) {
			if (!folder.isEmpty()) {
				_session->local().writePref<QString>("video-export-parent", folder);
				add(peer, folder);
			}
		}));
}

void VideoManager::Impl::show() {
	_timer.start(250);
	if (!_panel) {
		_panel = std::make_unique<QDialog>();
		_panel->setWindowTitle(tr::lng_video_export_title(tr::now)
			+ u" — "_q + _session->user()->name()
			+ u" ["_q + QString::number(_session->uniqueId()) + ']');
		_panel->setWindowModality(Qt::NonModal);
		_panel->resize(2 * st::boxWideWidth, st::boxWideWidth);
		_panel->setFont(st::normalFont->f);
		auto palette = _panel->palette();
		palette.setColor(QPalette::Window, st::boxBg->c);
		palette.setColor(QPalette::Base, st::boxBg->c);
		palette.setColor(QPalette::WindowText, st::boxTextFg->c);
		palette.setColor(QPalette::Text, st::boxTextFg->c);
		_panel->setPalette(palette);
		auto layout = new QVBoxLayout(_panel.get());
		const auto padding = st::boxPadding;
		layout->setContentsMargins(padding.left(), padding.top(), padding.right(), padding.bottom());
		layout->setSpacing(st::boxOptionListSkip);
		auto hint = new QLabel(tr::lng_video_export_hint(tr::now), _panel.get());
		hint->setWordWrap(true);
		layout->addWidget(hint);
		auto legend = new QLabel(
			u"↓ "_q + VideoStateText(State::Downloading)
			+ u" · ✓ "_q + VideoStateText(State::Complete)
			+ u" · ! "_q + VideoStateText(State::Failed)
			+ u" · Ⅱ "_q + VideoStateText(State::Paused)
			+ u" · × "_q + VideoStateText(State::Cancelled), _panel.get());
		legend->setWordWrap(true);
		layout->addWidget(legend);
		auto bypass = new QCheckBox(tr::lng_video_export_bypass(tr::now), _panel.get());
		bypass->setChecked(_session->settings().bypassCopyProtection());
		QObject::connect(bypass, &QCheckBox::toggled, [=](bool enabled) {
			_session->settings().setBypassCopyProtection(enabled);
			_session->saveSettings();
		});
		layout->addWidget(bypass);
		_summary = new QLabel(_panel.get());
		layout->addWidget(_summary);
		_table = new QTableWidget(0, 5, _panel.get());
		_table->setHorizontalHeaderLabels({
			tr::lng_video_export_chat(tr::now),
			tr::lng_video_export_status(tr::now),
			tr::lng_video_export_progress(tr::now),
			tr::lng_video_export_details(tr::now),
			tr::lng_video_export_chat_id(tr::now),
		});
		_table->setSelectionBehavior(QAbstractItemView::SelectRows);
		_table->setSelectionMode(QAbstractItemView::SingleSelection);
		_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		_table->verticalHeader()->hide();
		_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
		_table->setColumnWidth(0, st::boxWideWidth / 2);
		_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
		_table->setColumnWidth(2, st::boxWideWidth / 3);
		_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
		layout->addWidget(_table);
		auto buttons = new QHBoxLayout();
		_pause = new QPushButton(tr::lng_video_export_pause(tr::now), _panel.get());
		_cancel = new QPushButton(tr::lng_video_export_cancel(tr::now), _panel.get());
		_retry = new QPushButton(tr::lng_video_export_retry(tr::now), _panel.get());
		auto folder = new QPushButton(tr::lng_video_export_open_folder(tr::now), _panel.get());
		auto hide = new QPushButton(tr::lng_video_export_hide(tr::now), _panel.get());
		for (const auto button : { _pause, _cancel, _retry, folder, hide }) {
			buttons->addWidget(button);
		}
		layout->addLayout(buttons);
		QObject::connect(_pause, &QPushButton::clicked, [=] {
			if (const auto job = selected()) {
				if (job->state == State::Paused) {
					resume(*job);
				} else {
					stop(*job, State::Paused);
				}
			}
		});
		QObject::connect(_cancel, &QPushButton::clicked, [=] {
			if (const auto job = selected()) {
				stop(*job, State::Cancelled);
			}
		});
		QObject::connect(_retry, &QPushButton::clicked, [=] {
			if (const auto job = selected()) {
				prepare(*job);
			}
		});
		QObject::connect(folder, &QPushButton::clicked, [=] {
			if (const auto job = selected()) {
				QDesktopServices::openUrl(QUrl::fromLocalFile(job->folder));
			}
		});
		QObject::connect(hide, &QPushButton::clicked, [=] { _panel->hide(); });
		QObject::connect(_table, &QTableWidget::itemSelectionChanged, [=] { refresh(); });
	}
	_panel->show();
	_panel->raise();
	_panel->activateWindow();
	refresh();
}

void VideoManager::Impl::add(not_null<PeerData*> peer, const QString &parent) {
	const auto normalizedParent = QDir::cleanPath(QDir(parent).absolutePath());
	const auto folder = QDir(normalizedParent).filePath(
		u"account_"_q + QString::number(_session->uniqueId()) + '/'
		+ VideoFiles::SafeName(peer->name()) + '_' + QString::number(peer->id.value));
	for (const auto &job : _jobs) {
		if (job->peer == peer && job->parent == normalizedParent) {
			_table->selectRow(int(&job - _jobs.data()));
			if (!job->ioPending
				&& (job->state == State::Complete || job->state == State::Failed || job->state == State::Cancelled)) {
				prepare(*job);
			}
			return;
		}
	}
	auto owned = std::make_unique<Job>(peer);
	const auto job = owned.get();
	job->parent = normalizedParent;
	job->folder = folder;
	job->cancelled = std::make_shared<std::atomic<bool>>(false);
	job->manifest.account = QString::number(_session->uniqueId());
	job->manifest.peer = QString::number(peer->id.value);
	job->manifest.title = peer->name();
	job->sources.push_back(peer);
	if (const auto migrated = peer->migrateFrom()) {
		job->sources.push_back(migrated);
	}
	_jobs.push_back(std::move(owned));
	prepare(*job);
	refresh();
	_table->selectRow(int(_jobs.size()) - 1);
}

void VideoManager::Impl::prepare(Job &value) {
	_timer.start(250);
	const auto job = &value;
	if (job->ioPending || job->loader || job->request) {
		return;
	}
	job->lock = nullptr;
	job->videos.clear();
	job->discovered.clear();
	job->source = job->offset = job->snapshot = job->scanned = 0;
	job->scanAttempts = 0;
	job->current = -1;
	job->waitUntil = 0;
	job->needsSave = false;
	job->state = State::Preparing;
	job->error.clear();
	job->cancelled = std::make_shared<std::atomic<bool>>(false);
	job->manifest = VideoFiles::Manifest{
		QString::number(_session->uniqueId()),
		QString::number(job->peer->id.value),
		job->peer->name(),
	};
	job->ioPending = true;
	const auto parent = job->parent;
	const auto initial = job->manifest;
	const auto cancelled = job->cancelled;
	_workers.start([=, weak = base::make_weak(this)] {
		const auto folder = VideoFiles::ChatFolder(
			parent, initial.account, initial.peer, initial.title);
		crl::on_main(weak, [=] {
			job->folder = folder;
			refresh();
		});
		auto manifest = initial;
		auto error = QString();
		auto lock = std::make_shared<std::unique_ptr<QLockFile>>();
		if (!QDir().mkpath(folder)) {
			error = u"Could not create the destination folder."_q;
		} else {
			*lock = std::make_unique<QLockFile>(QDir(folder).filePath(u".video-export.lock"_q));
			if (!(*lock)->tryLock()) {
				error = u"Another export is using this folder."_q;
			} else {
				error = VideoFiles::Read(folder, manifest);
				if (error.isEmpty()) {
					for (auto &entry : manifest.entries) {
						if (!VideoFiles::Complete(folder, entry, cancelled.get())) {
							const auto path = QDir(folder).filePath(entry.name);
							if (entry.status == u"complete"_q
								&& QFileInfo::exists(path)
								&& !cancelled->load()) {
								const auto quarantine = path + '.'
									+ QUuid::createUuid().toString(QUuid::WithoutBraces) + u".corrupt"_q;
								if (!QFile::rename(path, quarantine)) {
									error = u"Could not preserve an incomplete export file."_q;
									break;
								}
							}
							entry.status = u"pending"_q;
						}
					}
				}
				if (cancelled->load()) {
					error = u"Cancelled"_q;
				} else if (error.isEmpty()) {
					error = VideoFiles::Write(folder, manifest);
				}
			}
		}
		crl::on_main(weak, [=, manifest = std::move(manifest)]() mutable {
			job->ioPending = false;
			job->folder = folder;
			job->manifest = std::move(manifest);
			job->lock = std::move(*lock);
			job->error = error;
			if (job->state == State::Preparing) {
				job->state = error.isEmpty() ? State::Scanning : State::Failed;
			}
			if (!error.isEmpty() && error != u"Cancelled"_q) {
				job->lock = nullptr;
			}
			pump();
			refresh();
		});
	});
}

void VideoManager::Impl::scan(Job &job) {
	const auto source = job.sources[job.source];
	job.request = _api.request(MTPmessages_GetHistory(
		source->input(),
		MTP_int(job.offset),
		MTP_int(0), // offset_date
		MTP_int(0), // add_offset
		MTP_int(kPageSize),
		MTP_int(job.snapshot), // max_id, exclusive
		MTP_int(0), // min_id
		MTP_long(0)
	)).done([=, &job](const MTPmessages_Messages &result) {
		job.request = 0;
		job.scanAttempts = 0;
		receive(job, result);
	}).fail([=, &job](const MTP::Error &error) {
		job.request = 0;
		const auto type = error.type();
		if (type.startsWith(u"FLOOD_WAIT_"_q)
			|| type.startsWith(u"FLOOD_PREMIUM_WAIT_"_q)) {
			job.waitUntil = _clock.elapsed() + std::max(qint64(1), type.section('_', -1).toLongLong()) * 1000;
		} else if (++job.scanAttempts < kMaxAttempts) {
			job.waitUntil = _clock.elapsed() + 2000 * job.scanAttempts;
		} else {
			job.state = State::Failed;
			job.error = MTP::IgnoreError(error) ? u"History request interrupted."_q : type;
			job.needsSave = true;
		}
	}).handleFloodErrors().send();
}

void VideoManager::Impl::receive(Job &job, const MTPmessages_Messages &result) {
	auto messages = QVector<MTPMessage>();
	result.match([&](const MTPDmessages_messages &data) {
		messages = data.vmessages().v;
	}, [&](const MTPDmessages_messagesSlice &data) {
		messages = data.vmessages().v;
	}, [&](const MTPDmessages_channelMessages &data) {
		messages = data.vmessages().v;
	}, [&](const MTPDmessages_messagesNotModified &) {
		job.error = u"The server returned an unexpected history response."_q;
	});
	if (!job.error.isEmpty()) {
		job.state = State::Failed;
		job.needsSave = true;
		return;
	}
	if (messages.isEmpty()) {
		if (++job.source < int(job.sources.size())) {
			job.offset = job.snapshot = 0;
		} else {
			job.manifest.scanComplete = true;
			job.state = State::Downloading;
			job.needsSave = true;
		}
		return;
	}
	auto minimum = std::numeric_limits<int>::max();
	auto maximum = 0;
	const auto source = job.sources[job.source];
	for (const auto &message : messages) {
		message.match([&](const MTPDmessage &data) {
			const auto id = data.vid().v;
			minimum = std::min(minimum, id);
			maximum = std::max(maximum, id);
			if (const auto value = data.vmedia()) {
				media(
					job,
					FullMsgId(source->id, MsgId(id)),
					*value,
					data.is_noforwards() || !source->allowsForwarding());
			}
		}, [&](const MTPDmessageService &data) {
			minimum = std::min(minimum, data.vid().v);
			maximum = std::max(maximum, data.vid().v);
		}, [&](const MTPDmessageEmpty &data) {
			minimum = std::min(minimum, data.vid().v);
			maximum = std::max(maximum, data.vid().v);
		});
	}
	job.scanned += messages.size();
	if (minimum <= 0 || (job.offset && minimum >= job.offset)) {
		job.error = u"History pagination stopped advancing; completeness cannot be confirmed."_q;
		job.state = State::Failed;
	} else {
		job.offset = minimum;
		if (!job.snapshot) {
			job.snapshot = maximum + 1;
		}
	}
	job.needsSave = true;
}

void VideoManager::Impl::media(
		Job &job,
		FullMsgId origin,
		const MTPMessageMedia &value,
		bool requiresBypass) {
	if (value.type() == mtpc_messageMediaPaidMedia) {
		for (const auto &extended : value.c_messageMediaPaidMedia().vextended_media().v) {
			if (extended.type() == mtpc_messageExtendedMedia) {
				media(job, origin, extended.c_messageExtendedMedia().vmedia(), requiresBypass);
			} else if (extended.c_messageExtendedMediaPreview().vvideo_duration()) {
				++job.manifest.unavailable;
			}
		}
		return;
	}
	if (value.type() != mtpc_messageMediaDocument) {
		return;
	}
	const auto &data = value.c_messageMediaDocument();
	const auto mtp = data.vdocument();
	if (!mtp || mtp->type() != mtpc_document) {
		if (data.is_video() || data.is_round()) {
			++job.manifest.unavailable;
		}
		return;
	}
	const auto document = _session->data().processDocument(*mtp);
	if (!document->isVideoFile()
		&& !document->isVideoMessage()
		&& !document->mimeString().startsWith(u"video/"_q)) {
		return;
	}
	if (requiresBypass && !_session->settings().bypassCopyProtection()) {
		++job.manifest.unavailable;
		return;
	}
	if (document->size <= 0 || !document->hasRemoteLocation()) {
		++job.manifest.unavailable;
		return;
	}
	auto entry = VideoFiles::Entry{
		QString::number(origin.peer.value),
		QString::number(origin.msg.bare),
		QString::number(document->id),
		QString(),
		document->size,
		u"pending"_q,
	};
	const auto key = VideoFiles::Key(entry);
	if (job.discovered.contains(key)) {
		return;
	}
	job.discovered.insert(key);
	entry.name = VideoFiles::FileName(entry, document->filename());
	const auto old = job.manifest.entries.constFind(key);
	if (old != job.manifest.entries.cend() && old->size == entry.size) {
		entry.name = old->name;
		if (old->status == u"complete"_q) {
			entry.status = old->status;
			entry.sha256 = old->sha256;
			_completed.insert(document->id, QDir(job.folder).filePath(entry.name));
		}
	}
	job.videos.push_back({
		std::move(entry),
		document,
		origin,
		0,
		requiresBypass || (data.vttl_seconds() != nullptr),
	});
}

void VideoManager::Impl::pump() {
	auto scans = 0;
	auto active = 0;
	for (const auto &job : _jobs) {
		scans += (job->request != 0);
		active += (job->loader != nullptr || job->ioPending);
		if (job->needsSave && !job->ioPending && !job->loader) {
			save(*job);
		}
	}
	for (auto i = 0; i != int(_jobs.size()); ++i) {
		const auto index = (_roundRobin + i) % int(_jobs.size());
		auto &job = *_jobs[index];
		if (job.ioPending || job.waitUntil > _clock.elapsed()) {
			continue;
		}
		if (job.state == State::Scanning && !job.request && scans < kParallelScans) {
			scan(job);
			++scans;
		}
		if ((job.state == State::Downloading || job.state == State::Scanning)
			&& !job.loader) {
			auto next = -1;
			auto pending = false;
			auto failed = job.manifest.unavailable > 0;
			for (auto j = 0; j != int(job.videos.size()); ++j) {
				const auto &video = job.videos[j];
				failed |= (video.entry.status == u"failed"_q);
				if (video.entry.status == u"pending"_q) {
					pending = true;
					if (next < 0 && !_activeDocuments.contains(video.document->id)) {
						next = j;
					}
				}
			}
			if (!pending && job.manifest.scanComplete) {
				job.state = failed ? State::Failed : State::Complete;
				job.needsSave = true;
			} else if (next >= 0 && active < kParallelDownloads) {
				download(job, next);
				++active;
			}
		}
	}
	if (!_jobs.empty()) {
		_roundRobin = (_roundRobin + 1) % int(_jobs.size());
	}
	const auto running = ranges::any_of(_jobs, [](const auto &job) {
		return job->state == State::Preparing
			|| job->state == State::Scanning
			|| job->state == State::Downloading
			|| job->ioPending;
	});
	base::UpdatePowerSaveBlocker(
		_keepAwake,
		running,
		base::PowerSaveBlockType::PreventAppSuspension,
		[] { return tr::lng_video_export_title(tr::now); },
		[] { return static_cast<QWindow*>(nullptr); });
	if (!running && (!_panel || !_panel->isVisible())) {
		_timer.stop();
	}
}

void VideoManager::Impl::download(Job &job, int index) {
	job.current = index;
	_activeDocuments.insert(job.videos[index].document->id);
	const auto generation = ++job.generation;
	auto &video = job.videos[index];
	if (!_session->settings().bypassCopyProtection()
		&& (video.requiresBypass || !job.peer->allowsForwarding())) {
		finish(job, generation, u"Copy protection is enabled."_q, false);
		return;
	}
	const auto path = QDir(job.folder).filePath(video.entry.name);
	const auto saved = _completed.value(video.document->id);
	const auto reuse = saved.isEmpty() ? video.document->filepath(true) : saved;
	const auto cancelled = job.cancelled;
	const auto size = video.entry.size;
	auto manifest = job.manifest;
	for (const auto &video : job.videos) {
		manifest.entries.insert(VideoFiles::Key(video.entry), video.entry);
	}
	const auto key = VideoFiles::Key(video.entry);
	const auto folder = job.folder;
	const auto partial = path + '.' + QUuid::createUuid().toString(QUuid::WithoutBraces) + u".partial"_q;
	job.ioPending = true;
	_workers.start([=, &job, weak = base::make_weak(this)]() mutable {
		auto error = QString();
		auto copied = false;
		const auto disk = QStorageInfo(folder);
		if (QFileInfo::exists(path)) {
			error = u"Destination already exists; it was not overwritten."_q;
		} else if (disk.isValid() && disk.bytesAvailable() >= 0 && disk.bytesAvailable() < size) {
			error = u"Not enough free disk space."_q;
		} else if (!reuse.isEmpty() && QFileInfo(reuse).size() == size) {
			auto &entry = manifest.entries[key];
			entry.sha256 = VideoFiles::Checksum(reuse, cancelled.get());
			entry.status = u"complete"_q;
			if (entry.sha256.isEmpty()) {
				error = u"Could not verify the reusable file."_q;
			} else {
				error = VideoFiles::Write(folder, manifest);
				if (error.isEmpty()) {
					error = VideoFiles::Copy(reuse, path, size, *cancelled, entry.sha256);
				}
			}
			copied = error.isEmpty();
		}
		crl::on_main(weak, [=, &job] {
			job.ioPending = false;
			if (generation != job.generation) {
				return;
			}
			if (copied || !error.isEmpty()) {
				if (copied) {
					job.videos[job.current].entry.sha256 = manifest.entries[key].sha256;
				}
				finish(job, generation, error, false);
				return;
			}
			if (job.state != State::Downloading && job.state != State::Scanning) {
				finish(job, generation, u"Cancelled"_q, false);
				return;
			}
			auto &video = job.videos[job.current];
			++video.attempts;
			job.loader = std::make_unique<mtpFileLoader>(
				_session,
				video.document->videoPreloadLocation(),
				video.origin,
				UnknownFileLocation,
				partial,
				size,
				size,
				LoadToFileOnly,
				LoadFromCloudOrLocal,
				false,
				video.document->cacheTag());
			job.loader->updates() | rpl::on_next_error_done(
				[] {},
				[=, &job](const FileLoader::Error &error) {
					const auto text = error.failureReason == FileLoader::FailureReason::FileWriteFailure
						? u"Could not write the video to disk."_q
						: u"Telegram could not download this video."_q;
					crl::on_main(weak, [=, &job] { finish(job, generation, text, false); });
				},
				[=, &job] {
					crl::on_main(weak, [=, &job] { finish(job, generation, QString(), true); });
				},
				job.loading);
			job.loader->start();
		});
	});
}

void VideoManager::Impl::finish(
		Job &job,
		int generation,
		QString error,
		bool publish) {
	if (generation != job.generation || job.current < 0) {
		return;
	}
	const auto partial = job.loader ? job.loader->fileName() : QString();
	job.loading.destroy();
	job.loader = nullptr;
	auto &video = job.videos[job.current];
	const auto path = QDir(job.folder).filePath(video.entry.name);
	if (publish) {
		job.ioPending = true;
		const auto size = video.entry.size;
		auto manifest = job.manifest;
		for (const auto &video : job.videos) {
			manifest.entries.insert(VideoFiles::Key(video.entry), video.entry);
		}
		const auto key = VideoFiles::Key(video.entry);
		const auto folder = job.folder;
		const auto cancelled = job.cancelled;
		_workers.start([=, &job, weak = base::make_weak(this)]() mutable {
			auto &entry = manifest.entries[key];
			entry.sha256 = VideoFiles::Checksum(partial, cancelled.get());
			entry.status = u"complete"_q;
			auto error = QString();
			if (cancelled->load()) {
				error = u"Cancelled"_q;
			} else if (entry.sha256.isEmpty()) {
				error = u"Could not verify the downloaded file."_q;
			} else {
				error = VideoFiles::Write(folder, manifest);
				if (error.isEmpty()) {
					error = VideoFiles::Publish(partial, path, size);
				}
			}
			if (!error.isEmpty()) {
				QFile::remove(partial);
			}
			crl::on_main(weak, [=, &job] {
				job.ioPending = false;
				job.videos[job.current].entry.sha256 = entry.sha256;
				finish(job, generation, error, false);
			});
		});
		return;
	}
	video.entry.error = error;
	_activeDocuments.remove(video.document->id);
	if (error.isEmpty()) {
		video.entry.status = u"complete"_q;
		_completed.insert(video.document->id, path);
		video.document->setLocation(Core::FileLocation(path));
		_session->local().writeFileLocation(video.document->mediaKey(), Core::FileLocation(path));
	} else if (error == u"Telegram could not download this video."_q && video.attempts < kMaxAttempts) {
		job.waitUntil = _clock.elapsed() + 2000 * video.attempts;
	} else if (error != u"Cancelled"_q) {
		video.entry.status = u"failed"_q;
	}
	job.current = -1;
	job.needsSave = true;
}

void VideoManager::Impl::save(Job &job) {
	if (!job.lock || !job.lock->isLocked()) {
		job.needsSave = false;
		return;
	}
	for (const auto &video : job.videos) {
		job.manifest.entries.insert(VideoFiles::Key(video.entry), video.entry);
	}
	job.manifest.state = StateKey(job.state);
	const auto manifest = job.manifest;
	const auto folder = job.folder;
	job.ioPending = true;
	job.needsSave = false;
	_workers.start([=, &job, weak = base::make_weak(this)] {
		const auto error = VideoFiles::Write(folder, manifest);
		crl::on_main(weak, [=, &job] {
			job.ioPending = false;
			if (!error.isEmpty()) {
				job.state = State::Failed;
				job.error = u"Could not save the export manifest: "_q + error;
			}
		});
	});
}

void VideoManager::Impl::stop(Job &job, State state) {
	job.state = state;
	job.cancelled->store(true);
	if (job.request) {
		_api.request(base::take(job.request)).cancel();
	}
	if (job.loader) {
		_activeDocuments.remove(job.videos[job.current].document->id);
		++job.generation;
		job.loading.destroy();
		job.loader = nullptr;
		job.current = -1;
	}
	job.needsSave = true;
	refresh();
}

void VideoManager::Impl::resume(Job &job) {
	if (job.ioPending || !job.lock || !job.lock->isLocked()) {
		return;
	}
	job.cancelled = std::make_shared<std::atomic<bool>>(false);
	job.state = job.manifest.scanComplete ? State::Downloading : State::Scanning;
	job.error.clear();
	job.needsSave = true;
	pump();
	refresh();
}

VideoManager::Impl::Job *VideoManager::Impl::selected() {
	const auto index = _table->currentRow();
	return (index >= 0 && index < int(_jobs.size())) ? _jobs[index].get() : nullptr;
}

void VideoManager::Impl::refresh() {
	refreshStates();
	if (!_panel || !_panel->isVisible()) {
		return;
	}
	auto total = 0;
	auto doneTotal = 0;
	while (_table->rowCount() < int(_jobs.size())) {
		const auto row = _table->rowCount();
		_table->insertRow(row);
		for (const auto column : { 0, 1, 3, 4 }) {
			_table->setItem(row, column, new QTableWidgetItem());
		}
		_table->setCellWidget(row, 2, new QProgressBar(_table));
	}
	for (auto row = 0; row != int(_jobs.size()); ++row) {
		auto &job = *_jobs[row];
		auto done = 0;
		auto failed = job.manifest.unavailable;
		auto bytes = qint64(0);
		auto bytesTotal = qint64(0);
		auto firstError = QString();
		for (const auto &video : job.videos) {
			bytesTotal += video.entry.size;
			if (video.entry.status == u"complete"_q) {
				++done;
				bytes += video.entry.size;
			} else if (video.entry.status == u"failed"_q) {
				++failed;
				if (firstError.isEmpty()) {
					firstError = video.entry.name + u": "_q + video.entry.error;
				}
			}
		}
		if (job.loader) {
			bytes += job.loader->currentOffset();
		}
		const auto now = _clock.elapsed();
		if (now - job.speedTime >= 1000) {
			job.speed = std::max(qint64(0), bytes - job.speedBytes) * 1000. / (now - job.speedTime);
			job.speedBytes = bytes;
			job.speedTime = now;
		}
		auto status = VideoStateText(job.state);
		if (job.waitUntil > now) {
			status += u" ("_q + QString::number((job.waitUntil - now + 999) / 1000) + u" s)"_q;
		}
		auto details = QString::number(done) + u" / "_q + QString::number(job.videos.size())
			+ u" · "_q + Bytes(bytes) + u" / "_q + Bytes(bytesTotal);
		if (job.state == State::Scanning) {
			details += u" · "_q + QString::number(job.scanned) + u" messages"_q;
		}
		if (failed) {
			details += u" · "_q + QString::number(failed) + u" errors / unavailable"_q;
		}
		if (job.loader && job.speed > 0.) {
			details += u" · "_q + Bytes(qint64(job.speed)) + u"/s · ~"_q
				+ QString::number(int((bytesTotal - bytes) / job.speed / 60.) + 1) + u" min"_q;
		}
		if (!job.error.isEmpty()) {
			details += u" · "_q + job.error;
		} else if (!firstError.isEmpty()) {
			details += u" · "_q + firstError;
		}
		_table->item(row, 0)->setText(job.manifest.title);
		_table->item(row, 0)->setToolTip(
			job.peer->name() + u"\nID: "_q + QString::number(job.peer->id.value)
			+ '\n' + job.folder);
		_table->item(row, 4)->setText(QString::number(job.peer->id.value));
		_table->item(row, 1)->setText(status);
		_table->item(row, 3)->setText(details);
		_table->item(row, 3)->setToolTip(details + '\n' + job.folder);
		const auto progress = static_cast<QProgressBar*>(_table->cellWidget(row, 2));
		progress->setRange(0, job.manifest.scanComplete ? 1000 : 0);
		progress->setValue(bytesTotal ? int(bytes * 1000 / bytesTotal) : 0);
		total += job.videos.size();
		doneTotal += done;
	}
	_summary->setText(QString::number(_jobs.size()) + u" chats · "_q
		+ QString::number(doneTotal) + u" / "_q + QString::number(total) + u" videos"_q);
	const auto job = selected();
	_pause->setText(job && job->state == State::Paused
		? tr::lng_video_export_resume(tr::now)
		: tr::lng_video_export_pause(tr::now));
	_pause->setEnabled(job && !job->ioPending
		&& (job->state == State::Scanning || job->state == State::Downloading || job->state == State::Paused));
	_cancel->setEnabled(job && job->state != State::Complete && job->state != State::Cancelled);
	_retry->setEnabled(job && !job->ioPending && !job->loader && !job->request
		&& (job->state == State::Failed || job->state == State::Cancelled));
}

State VideoManager::Impl::state(not_null<PeerData*> peer) const {
	Expects(&peer->session() == _session.get());
	return _states.value(QString::number(peer->id.value), State::None);
}

void VideoManager::Impl::refreshStates() {
	if (!_restoreAnnounced) {
		_restoreAnnounced = true;
		for (auto i = _states.begin(); i != _states.end(); ++i) {
			if (const auto history = _session->data().historyLoaded(PeerId(i.key().toULongLong()))) {
				history->updateChatListEntry();
			}
		}
	}
	auto current = VideoExportStates();
	for (const auto &job : _jobs) {
		auto state = job->state;
		if (job->manifest.unavailable || ranges::any_of(job->videos, [](const Video &video) {
			return video.entry.status == u"failed"_q;
		})) {
			state = State::Failed;
		}
		const auto key = QString::number(job->peer->id.value);
		current[key] = CombineVideoExportStates(current.value(key, State::None), state);
	}
	applyStates(current, true);
}

void VideoManager::Impl::applyStates(const VideoExportStates &states, bool overwrite) {
	auto changed = QStringList();
	for (auto i = states.begin(); i != states.end(); ++i) {
		if ((!_states.contains(i.key()) || overwrite)
			&& _states.value(i.key(), State::None) != i.value()) {
			_states.insert(i.key(), i.value());
			changed.push_back(i.key());
		}
	}
	if (changed.isEmpty()) {
		return;
	}
	_session->local().writePref<QString>(
		"video-export-statuses",
		SaveVideoExportStates(QString::number(_session->uniqueId()), _states));
	for (const auto &key : changed) {
		if (const auto history = _session->data().historyLoaded(PeerId(key.toULongLong()))) {
			history->updateChatListEntry();
		}
	}
}

VideoManager::VideoManager(not_null<Main::Session*> session)
: _impl(std::make_unique<Impl>(session)) {
}

VideoManager::~VideoManager() = default;

void VideoManager::chooseFolder(not_null<PeerData*> peer) {
	_impl->chooseFolder(peer);
}

void VideoManager::show() {
	_impl->show();
}

VideoExportState VideoManager::state(not_null<PeerData*> peer) const {
	return _impl->state(peer);
}

QString VideoManager::StateText(VideoExportState state) {
	return VideoStateText(state);
}

} // namespace Export
