#pragma once

#include <cstdint>

namespace mohpcf::subtitles {

// Subtitle size. The engine object keeps two subtitle font sizes in pixels made for 720p (21 and 21.75,
// right after its SubtitleFontName "$DefaultFont"); at 4K they come out a third of the intended height.
// A background thread finds both copies (class default + live engine object) in the game's memory and
// multiplies the sizes; it keeps them that way if the game rewrites them. Tested live: the subtitles
// grow at once.

// scale <= 0: automatic (render height / 720).
void start(float scale);
void set_scale(float scale);
float configured_scale();
float applied_scale();
std::int32_t fields_found();

}  // namespace mohpcf::subtitles
