#pragma once

#include <string>

#include "ImageToFramebufferDecoder.h"

class GifToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  static bool supportsFormat(const std::string& extension);

  bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                            const RenderConfig& config) override;

  bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const override;

  const char* getFormatName() const override { return "GIF"; }
};
