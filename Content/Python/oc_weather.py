"""Weather presets for UOCWorldStateSubsystem: /Game/Core/Weather/DA_Weather_{Clear,Cloudy,Rain}.

  scripts/run_python.sh Content/Python/oc_weather.py

Values are multipliers on the map's lighting (sun, sky light, fog) plus the sky atmosphere's
Rayleigh/Mie scales (grey, hazy skies), RainAmount and the wetness the weather drives toward.
Materials read RainAmount and Wetness from MPC_World (one value drives wet surfaces, puddles and
ripples, ROADMAP M4b).
"""

import unreal

import oc_lib as L

TAG = "oc_weather"
PRESETS = {
    "Clear":  dict(sun_scale=1.0,  sky_light_scale=1.0,  fog_scale=1.0, rayleigh_scale=1.0,  mie_scale=1.0, rain_amount=0.0, wetness_target=0.0, exposure_bias=0.0, wind_strength=0.25),
    # Overcast: the sun barely casts shadows, the sky light carries the scene, the sky goes grey.
    "Cloudy": dict(sun_scale=0.15, sky_light_scale=1.15, fog_scale=1.8, rayleigh_scale=0.4,  mie_scale=4.0, rain_amount=0.0, wetness_target=0.0, exposure_bias=0.0, wind_strength=0.55),
    # The sky light captures the sky in real time, and the sky is lit by the (dimmed) sun, so its
    # scale has to rise as the sun falls; exposure_bias (EV, day only) stops the bright fog at the end
    # of a street from exposing the shaded walls down to black (QA 2026-11-09).
    "Rain":   dict(sun_scale=0.04, sky_light_scale=4.5,  fog_scale=3.5, rayleigh_scale=0.3,  mie_scale=5.0, rain_amount=1.0, wetness_target=1.0, exposure_bias=1.0, wind_strength=0.9),
}

for name, props in PRESETS.items():
    da = L.data_asset(f"DA_Weather_{name}", "/Game/Core/Weather", unreal.OCWeatherPreset, TAG)
    L.set_props(da, **props)
    L.save(da)
    L.log(TAG, f"DA_Weather_{name}: {props}")
