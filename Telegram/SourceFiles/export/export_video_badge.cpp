#include "export/export_video_badge.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Export {

void PaintVideoExportBadge(
		QPainter &p,
		const QRectF &rect,
		VideoExportState state,
		const QColor &foreground,
		const QColor &background,
		qreal stroke) {
	if (state == VideoExportState::None) {
		return;
	}
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.translate(rect.topLeft());
	const auto size = rect.width();
	const auto point = [&](qreal x, qreal y) { return QPointF(x * size, y * size); };
	auto pen = QPen(foreground, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(background);
	p.drawEllipse(QRectF(stroke, stroke, size - 2 * stroke, size - 2 * stroke));
	p.setBrush(Qt::NoBrush);
	auto path = QPainterPath();
	switch (state) {
	case VideoExportState::None: break;
	case VideoExportState::Preparing:
	case VideoExportState::Scanning:
	case VideoExportState::Downloading:
		path.moveTo(point(.5, .25));
		path.lineTo(point(.5, .65));
		path.moveTo(point(.3, .48));
		path.lineTo(point(.5, .68));
		path.lineTo(point(.7, .48));
		break;
	case VideoExportState::Complete:
		path.moveTo(point(.28, .52));
		path.lineTo(point(.44, .68));
		path.lineTo(point(.72, .35));
		break;
	case VideoExportState::Failed:
		path.moveTo(point(.5, .28));
		path.lineTo(point(.5, .52));
		p.drawPoint(point(.5, .72));
		break;
	case VideoExportState::Paused:
		path.moveTo(point(.38, .3));
		path.lineTo(point(.38, .7));
		path.moveTo(point(.62, .3));
		path.lineTo(point(.62, .7));
		break;
	case VideoExportState::Cancelled:
		path.moveTo(point(.34, .34));
		path.lineTo(point(.66, .66));
		path.moveTo(point(.66, .34));
		path.lineTo(point(.34, .66));
		break;
	}
	p.drawPath(path);
	p.restore();
}

} // namespace Export
