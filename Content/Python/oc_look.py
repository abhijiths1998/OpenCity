"""Marevia post-process values (ART_BIBLE.md), shared by the world build (oc_m3_world.setup_look)
and oc_m4b_look.py, which applies them to the existing map without a rebuild.

Local exposure (M4b): with one exposure for the frame, a low sun's bright facades crushed shaded
streets to black at golden hour; more sky light barely reaches street canyons (Lumen occludes it
correctly). Local exposure lowers contrast per region instead: shadows lift, highlights hold.
"""

import unreal

POST_SETTINGS = dict(
    override_auto_exposure_min_brightness=True, auto_exposure_min_brightness=-1.0,
    override_auto_exposure_max_brightness=True, auto_exposure_max_brightness=3.0,
    override_auto_exposure_bias=True, auto_exposure_bias=0.3,
    override_local_exposure_shadow_contrast_scale=True, local_exposure_shadow_contrast_scale=0.6,
    override_local_exposure_highlight_contrast_scale=True, local_exposure_highlight_contrast_scale=0.8,
    override_motion_blur_amount=True, motion_blur_amount=0.15,
    override_motion_blur_max=True, motion_blur_max=2.0,
    override_bloom_intensity=True, bloom_intensity=0.35,
    override_vignette_intensity=True, vignette_intensity=0.25,
    override_white_temp=True, white_temp=6200.0,
)


def apply(st):
    """Set POST_SETTINGS, the LUT and the rain post-process on a PostProcessSettings struct."""
    for k, v in POST_SETTINGS.items():
        st.set_editor_property(k, v)
    lut = unreal.load_asset("/Game/Environment/Textures/LUT/T_LUT_Marevia")
    if lut:  # Tools/make_lut.py: ART_BIBLE grade (warm highlights, cool shadows)
        st.set_editor_property("override_color_grading_lut", True)
        st.set_editor_property("color_grading_lut", lut)
        st.set_editor_property("override_color_grading_intensity", True)
        st.set_editor_property("color_grading_intensity", 1.0)
    rain = unreal.load_asset("/Game/Materials/M_PP_Rain")
    if rain:  # rain streaks and dimming, scaled by MPC_World.RainAmount (free when it isn't raining)
        wb = unreal.WeightedBlendable()
        wb.set_editor_property("weight", 1.0)
        wb.set_editor_property("object", rain)
        blend = st.get_editor_property("weighted_blendables")
        blend.set_editor_property("array", [wb])
        st.set_editor_property("weighted_blendables", blend)
    return st
