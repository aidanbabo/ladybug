#include "draw.hpp"

#include "include/core/SkFontMetrics.h"

DrawCommand::DrawCommand(SkRect rect)
	: rect(rect)
{}

DrawText::DrawText(SkRect rect, std::string text, std::shared_ptr<SkFont> font, SkColor color)
	: DrawCommand(rect)
	, text(text)
	, font(font)
	, color(color)
{}

std::shared_ptr<DrawText> DrawText::create(float left, float top, std::string text, std::shared_ptr<SkFont> font, SkColor color) {
	float width = font->measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
	return DrawText::create(left, top, width, std::move(text), std::move(font), color);
}

std::shared_ptr<DrawText> DrawText::create(float left, float top, float width, std::string text, std::shared_ptr<SkFont> font, SkColor color) {
	SkFontMetrics m;
	font->getMetrics(&m);
	float height = m.fDescent - m.fAscent;
	return DrawText::create(left, top, width, height, std::move(text), std::move(font), color);
}

std::shared_ptr<DrawText> DrawText::create(float left, float top, float width, float height, std::string text, std::shared_ptr<SkFont> font, SkColor color) {
	return DrawText::create(SkRect::MakeXYWH(left, top, width, height), text, font, color);
}

std::shared_ptr<DrawText> DrawText::create(SkRect rect, std::string text, std::shared_ptr<SkFont> font, SkColor color) {
	return std::make_shared<DrawText>(rect, text, font, color);
}

void DrawText::execute(SkCanvas& canvas) {
	SkPaint paint;
	paint.setColor(color);
	paint.setAntiAlias(true);

	SkFontMetrics m;
	font->getMetrics(&m);
	float baseline = rect.fTop - m.fAscent;
	// Skia draws text from the baseline, not from the NW like Tkinter
	canvas.drawString(text.c_str(), rect.fLeft, baseline, *font, paint);
}

DrawRect::DrawRect(SkRect rect, SkColor color)
	: DrawCommand(rect)
	, color(color)
{}

std::shared_ptr<DrawRect> DrawRect::create(SkRect rect, SkColor color) {
	return std::make_shared<DrawRect>(rect, color);
}

void DrawRect::execute(SkCanvas& canvas) {
	SkPaint paint;
	paint.setColor(color);
	canvas.drawRect(rect, paint);
}

DrawOutline::DrawOutline(SkRect rect, SkColor color, float thickness)
	: DrawCommand(rect)
	, color(color)
	, thickness(thickness)
{}

std::shared_ptr<DrawOutline> DrawOutline::create(SkRect rect, SkColor color, float thickness) {
	return std::make_shared<DrawOutline>(rect, color, thickness);
}

void DrawOutline::execute(SkCanvas& canvas) {
	SkPaint paint;
	paint.setColor(color);
	paint.setStyle(SkPaint::kStroke_Style);
	paint.setStrokeWidth(thickness);
	canvas.drawRect(rect, paint);
}

DrawLine::DrawLine(float x1, float y1, float x2, float y2, SkColor color, float thickness)
	: DrawCommand(SkRect::MakeLTRB(x1, y1, x2, y2))
	, color(color)
	, thickness(thickness)
{}

std::shared_ptr<DrawLine> DrawLine::create(float x1, float y1, float x2, float y2, SkColor color, float thickness) {
	return std::make_shared<DrawLine>(x1, y1, x2, y2, color, thickness);
}

void DrawLine::execute(SkCanvas& canvas) {
	SkPath path = SkPath()
		.moveTo(rect.fLeft, rect.fTop)
		.lineTo(rect.fRight, rect.fBottom);
	SkPaint paint;
	paint.setColor(color);
	paint.setStrokeWidth(thickness);
	paint.setStyle(SkPaint::kStroke_Style);
	canvas.drawPath(path, paint);
	// canvas.drawLine(SkPoint::Make(rect.fLeft, rect.fTop), SkPoint::Make(rect.fRight, rect.fBottom), paint);
}

DrawRRect::DrawRRect(SkRect rect, float radius, SkColor color)
	: DrawCommand(rect)
	, rrect(SkRRect::MakeRectXY(rect, radius, radius))
	, color(color)
{}

std::shared_ptr<DrawRRect> DrawRRect::create(SkRect rect, float radius, SkColor color) {
	return std::make_shared<DrawRRect>(rect, radius, color);
}

void DrawRRect::execute(SkCanvas& canvas) {
	SkPaint paint;
	paint.setColor(color);
	canvas.drawRRect(rrect, paint);
}

Opacity::Opacity(float opacity, std::vector<std::shared_ptr<DrawCommand>> children)
	: DrawCommand(SkRect::MakeEmpty())
	, opacity(opacity)
	, children(std::move(children))
{
	for (auto const& cmd : children) {
		rect.join(cmd->rect);
	}
}

std::shared_ptr<Opacity> Opacity::create(float opacity, std::vector<std::shared_ptr<DrawCommand>> children) {
	return std::make_shared<Opacity>(opacity, std::move(children));
}

void Opacity::execute(SkCanvas& canvas) {
	SkPaint paint;
	paint.setAlphaf(opacity);
	canvas.saveLayer(nullptr, &paint);
	for (auto const& cmd : children) {
		cmd->execute(canvas);
	}
	canvas.restore();
}

static SkBlendMode parse_blend_mode(std::string_view blend_mode) {
	if (blend_mode == "multiply") {
		return SkBlendMode::kMultiply;
	} else if (blend_mode == "difference") {
		return SkBlendMode::kDifference;
	} else if (blend_mode == "destination-in") {
		return SkBlendMode::kDstIn;
	} else {
		return SkBlendMode::kSrcOver;
	}
}

Blend::Blend(std::string_view blend_mode, std::vector<std::shared_ptr<DrawCommand>> children)
	: DrawCommand(SkRect::MakeEmpty())
	, blend_mode(parse_blend_mode(blend_mode))
	, children(std::move(children))
{
	for (auto const& cmd : children) {
		rect.join(cmd->rect);
	}
}

std::shared_ptr<Blend> Blend::create(std::string_view blend_mode, std::vector<std::shared_ptr<DrawCommand>> children) {
	return std::make_shared<Blend>(blend_mode, std::move(children));
}

void Blend::execute(SkCanvas& canvas) {
	SkPaint paint;
	paint.setBlendMode(blend_mode);
	canvas.saveLayer(nullptr, &paint);
	for (auto const& cmd : children) {
		cmd->execute(canvas);
	}
	canvas.restore();
}
