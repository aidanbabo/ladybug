#pragma once
#include "include/core/SkRRect.h"
#include "include/core/SkFont.h"
#include "include/core/SkCanvas.h"

struct DrawCommand {
	SkRect rect;

	virtual void execute(SkCanvas& canvas) = 0;
	virtual ~DrawCommand() = default;
	explicit DrawCommand(SkRect rect);
};

struct DrawText : public DrawCommand {
	std::string text;
	std::shared_ptr<SkFont> font;
	SkColor color;

	static std::shared_ptr<DrawText> create(float left, float top, std::string text, std::shared_ptr<SkFont> font, SkColor color);
	static std::shared_ptr<DrawText> create(float left, float top, float width, std::string text, std::shared_ptr<SkFont> font, SkColor color);
	static std::shared_ptr<DrawText> create(float left, float top, float width, float height, std::string text, std::shared_ptr<SkFont> font, SkColor color);
	static std::shared_ptr<DrawText> create(SkRect rect, std::string text, std::shared_ptr<SkFont> font, SkColor color);

	void execute(SkCanvas& canvas) override;

	DrawText(SkRect rect, std::string text, std::shared_ptr<SkFont> font, SkColor color);
	~DrawText() = default;
};

struct DrawRect : public DrawCommand {
	SkColor color;

	static std::shared_ptr<DrawRect> create(SkRect rect, SkColor color);

	void execute(SkCanvas& canvas) override;

	DrawRect(SkRect rect, SkColor color);
	~DrawRect() = default;
};

struct DrawOutline : public DrawCommand {
	SkColor color;
	float thickness;

	static std::shared_ptr<DrawOutline> create(SkRect rect, SkColor color, float thickness);

	void execute(SkCanvas& canvas) override;

	DrawOutline(SkRect rect, SkColor color, float thickness);
	~DrawOutline() = default;
};

struct DrawLine : public DrawCommand {
	SkColor color;
	float thickness;

	static std::shared_ptr<DrawLine> create(float x1, float y1, float x2, float y2, SkColor color, float thickness);

	void execute(SkCanvas& canvas) override;

	DrawLine(float x1, float y1, float x2, float y2, SkColor color, float thickness);
	~DrawLine() = default;
};

struct DrawRRect : public DrawCommand {
	SkRRect rrect;
	SkColor color;

	static std::shared_ptr<DrawRRect> create(SkRect rect, float radius, SkColor color);

	void execute(SkCanvas& canvas) override;

	DrawRRect(SkRect rect, float radius, SkColor color);
	~DrawRRect() = default;
};

struct Opacity : public DrawCommand {
	float opacity;
	std::vector<std::shared_ptr<DrawCommand>> children;

	static std::shared_ptr<Opacity> create(float opacity, std::vector<std::shared_ptr<DrawCommand>> children);

	void execute(SkCanvas& canvas) override;

	Opacity(float opacity, std::vector<std::shared_ptr<DrawCommand>> children);
	~Opacity() = default;
};

struct Blend : public DrawCommand {
	SkBlendMode blend_mode;
	std::vector<std::shared_ptr<DrawCommand>> children;

	static std::shared_ptr<Blend> create(std::string_view blend_mode, std::vector<std::shared_ptr<DrawCommand>> children);

	void execute(SkCanvas& canvas) override;

	Blend(std::string_view blend_mode, std::vector<std::shared_ptr<DrawCommand>> children);
	~Blend() = default;
};
