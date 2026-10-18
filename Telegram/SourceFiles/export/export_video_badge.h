#pragma once

#include "export/export_video_status.h"
#include <QtCore/QRectF>
#include <QtGui/QColor>

class QPainter;

namespace Export {

void PaintVideoExportBadge(
	QPainter &p,
	const QRectF &rect,
	VideoExportState state,
	const QColor &foreground,
	const QColor &background,
	qreal stroke);

} // namespace Export
