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
    "Clear":  dict(sun_scale=1.0,  sky_light_scale=1.0,  fog_scale=1.0, rayleigh_scale=1.0,  mie_scale=1.0, rain_amount=0.0, wetness_target=0.0),
    # Overcast: the sun barely casts shadows, the sky light carries the scene, the sky goes grey.
    "Cloudy": dict(sun_scale=0.15, sky_light_scale=1.15, fog_scale=1.8, rayleigh_scale=0.4,  mie_scale=4.0, rain_amount=0.0, wetness_target=0.0),
    "Rain":   dict(sun_scale=0.08, sky_light_scale=1.0,  fog_scale=3.5, rayleigh_scale=0.3,  mie_scale=5.0, rain_amount=1.0, wetness_target=1.0),
}

for name, props in PRESETS.items():
    da = L.data_asset(f"DA_Weather_{name}", "/Game/Core/Weather", unreal.OCWeatherPreset, TAG)
    L.set_props(da, **props)
    L.save(da)
    L.log(TAG, f"DA_Weather_{name}: {props}")
