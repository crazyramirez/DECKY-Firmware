# Today backgrounds by weather — image prompts

One scene, 18 moods: **the same alpine valley** (lake, pine forest, mountain range) under each kind of
weather, by day and by night. Same camera, same place, so the backgrounds feel like one family when
they change with the weather.

## How to generate

The 18 images in this folder were made this way with the OpenAI Images API
(`firmware/tools/gen_weather_images.py`: gpt-image-2, 1536×1024, quality high, JPEG; it reads
`OPENAI_API_KEY` from the environment and skips the images that already exist).

- Model: gpt-image-2 (the screensaver wallpapers were made with it), **landscape 1536×1024**,
  highest quality, no text, no people, no watermark.
- Save each image as **JPEG** with the file name in the table, into this folder
  (`firmware/tools/weather_backgrounds/`). `tools/gen_weather_bg.py` fits it to every deck's screen
  (7" 1024×600, 4.3" 480×272, 3.5" portrait 320×480: centre crop).
- Start every prompt with the **base** below, then add the line of the table.

**Base (same for all 18):**

> Wide cinematic photograph of a quiet alpine valley: a calm lake in the lower third, dark pine
> forest on both shores, a mountain range on the horizon slightly right of centre. Camera at eye
> level from the shore, 24 mm lens. The upper half of the frame is a calm, dark, low-detail sky
> (it sits behind a large clock and white text), the interest sits in the lower half and the
> centre. Muted, deep tones, overall dark exposure (around −1.5 EV), soft contrast, no bright
> highlights near the top, photorealistic, no text, no people, no buildings, no watermark.

| File | Weather | Add to the base |
|---|---|---|
| `clear_day.jpg` | Clear | Clear deep-blue sky, late-afternoon sun low and to the right, warm golden light on the peaks, mirror-calm lake. |
| `clear_night.jpg` | Clear | Clear night, starry sky with a faint Milky Way, crescent moon low on the right, cold blue moonlight on the snow of the peaks, stars reflected in the lake. |
| `partly_day.jpg` | Partly cloudy | Blue sky with scattered white cumulus clouds, sun breaking through, patches of sunlight and shadow moving over the valley. |
| `partly_night.jpg` | Partly cloudy | Night, broken clouds lit from behind by the moon, a few stars between them, silver edges on the clouds. |
| `cloudy_day.jpg` | Overcast | Fully overcast flat grey sky, soft shadowless light, slate-grey lake, muted greens. |
| `cloudy_night.jpg` | Overcast | Overcast night, heavy low clouds faintly lit, almost no stars, very dark blue-grey palette. |
| `fog_day.jpg` | Fog | Thick morning fog over the lake, forest fading into white mist, peaks barely visible, soft diffuse light. |
| `fog_night.jpg` | Fog | Night fog, mist glowing faintly under a hidden moon, silhouettes of pines fading into darkness. |
| `drizzle_day.jpg` | Drizzle | Light drizzle, fine rain veil, soft grey clouds, tiny ripples on the lake, damp fresh greens. |
| `drizzle_night.jpg` | Drizzle | Night drizzle, fine rain visible against faint light, ripples on a dark lake, cool blue tones. |
| `rain_day.jpg` | Rain | Steady rain, dark heavy clouds, rain streaks visible over the forest, lake surface broken by raindrops. |
| `rain_night.jpg` | Rain | Rainy night, dark storm clouds, rain streaks catching faint light, wet reflective shore. |
| `storm_day.jpg` | Thunderstorm | Thunderstorm, dramatic dark anvil clouds over the peaks, a lightning bolt striking a distant mountain on the right, heavy rain curtains. |
| `storm_night.jpg` | Thunderstorm | Night thunderstorm, lightning illuminating the clouds from inside, a bolt in the distance on the right, violet-blue tones. |
| `snow_day.jpg` | Snow | Snowfall, everything covered in fresh snow, frozen lake with snow on it, white-grey sky, large soft flakes in the air. |
| `snow_night.jpg` | Snow | Snowy night, blue-hour darkness, snow-covered pines, falling snowflakes catching faint light, frozen lake. |
| `sleet_day.jpg` | Sleet / freezing rain | Sleet and freezing rain, icy glaze on the pine branches, grey sky, mixed rain and wet snow in the air. |
| `sleet_night.jpg` | Sleet / freezing rain | Night sleet, ice glistening on branches under faint light, mixed rain and snow, very cold blue palette. |

## Weather codes (Open-Meteo, WMO) → image

| Group | Codes |
|---|---|
| clear | 0 |
| partly | 1, 2 |
| cloudy | 3 |
| fog | 45, 48 |
| drizzle | 51, 53, 55 |
| rain | 61, 63, 65, 80, 81, 82 |
| storm | 95, 96, 99 |
| snow | 71, 73, 75, 77, 85, 86 |
| sleet | 56, 57, 66, 67 |

Day / night comes from the forecast (`is_day`). The temperature adds a light tint on top (cold below
5 °C, warm above 30 °C), so it needs no images of its own.
