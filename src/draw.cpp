#include "draw.hpp"

#include "include/core/SkFontMetrics.h"

DrawCommand::DrawCommand(SkRect rect)
	: rect(rect)
{}

DrawText::DrawText(SkRect rect,  std::string text, std::shared_ptr<SkFont> font, SkColor color)
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
	return std::make_shared<DrawText>(SkRect::MakeLTRB(left, top, left + width, top + height), text, font, color);
}

void DrawText::execute(float scroll, SkCanvas& canvas) {
	SkFontMetrics m;
	font->getMetrics(&m);
	SkPaint paint;
	paint.setColor(color);
	// Skia draws text from the baseline, not from the NW like Tkinter
	canvas.drawString(text.c_str(), rect.fLeft, rect.fTop - scroll - m.fAscent, *font, paint);
}

DrawRect::DrawRect(SkRect rect, SkColor color)
	: DrawCommand(rect)
	, color(color)
{}

std::shared_ptr<DrawRect> DrawRect::create(SkRect rect, SkColor color) {
	return std::make_shared<DrawRect>(rect, color);
}

void DrawRect::execute(float scroll, SkCanvas& canvas) {
	SkRect shifted = SkRect::MakeLTRB(rect.fLeft, rect.fTop - scroll, rect.fRight, rect.fBottom - scroll);
	SkPaint paint;
	paint.setColor(color);
	canvas.drawRect(shifted, paint);
}

DrawOutline::DrawOutline(SkRect rect, SkColor color, float thickness)
	: DrawCommand(rect)
	, color(color)
	, thickness(thickness)
{}

std::shared_ptr<DrawOutline> DrawOutline::create(SkRect rect, SkColor color, float thickness) {
	return std::make_shared<DrawOutline>(rect, color, thickness);
}

void DrawOutline::execute(float scroll, SkCanvas& canvas) {
	SkRect shifted = SkRect::MakeLTRB(rect.fLeft, rect.fTop - scroll, rect.fRight, rect.fBottom - scroll);
	SkPaint paint;
	paint.setColor(color);
	paint.setStyle(SkPaint::kStroke_Style);
	paint.setStrokeWidth(thickness);
	canvas.drawRect(shifted, paint);
}

DrawLine::DrawLine(float x1, float y1, float x2, float y2, SkColor color, float thickness)
	: DrawCommand(SkRect::MakeLTRB(x1, y1, x2, y2))
	, color(color)
	, thickness(thickness)
{}

std::shared_ptr<DrawLine> DrawLine::create(float x1, float y1, float x2, float y2, SkColor color, float thickness) {
	return std::make_shared<DrawLine>(x1, y1, x2, y2, color, thickness);
}

void DrawLine::execute(float scroll, SkCanvas& canvas) {
	SkPaint paint;
	paint.setColor(color);
	paint.setStrokeWidth(thickness);
	canvas.drawLine(SkPoint::Make(rect.fLeft, rect.fTop - scroll), SkPoint::Make(rect.fRight, rect.fBottom - scroll), paint);
}
