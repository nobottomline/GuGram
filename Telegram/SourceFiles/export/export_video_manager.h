#pragma once

#include "base/weak_ptr.h"
#include "export/export_video_status.h"

namespace Main {
class Session;
} // namespace Main

class PeerData;

namespace Export {

class VideoManager final {
public:
	explicit VideoManager(not_null<Main::Session*> session);
	~VideoManager();
	void chooseFolder(not_null<PeerData*> peer);
	void show();
	[[nodiscard]] VideoExportState state(not_null<PeerData*> peer) const;
	[[nodiscard]] static QString StateText(VideoExportState state);

private:
	class Impl;
	std::unique_ptr<Impl> _impl;

};

} // namespace Export
