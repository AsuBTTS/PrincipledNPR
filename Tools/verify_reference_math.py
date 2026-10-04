"""Deterministic scalar validation of the report's mathematical reference models.

Python standard library only. This is NOT an Unreal Engine, shader compiler,
GPU, texture filtering, derivative, image, or perceptual experiment.

Run from the project: python Tools/verify_reference_math.py
The script's own location determines the project and output path.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import random
import sys


PI = math.pi
RAD = PI / 180.0
EPS_WIDTH = 1e-5
SEED = 20261003
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Documentation" / "数学验证结果.json"


def clamp(x, lo=0.0, hi=1.0):
    return min(hi, max(lo, x))


def smoothstep(lo, hi, x):
    assert hi > lo
    t = clamp((x - lo) / (hi - lo))
    return t * t * (3.0 - 2.0 * t)


def band(x, center, half_width):
    # Scalar reference: artistic half width + epsilon only.
    # No finite pixel derivatives/fwidth are claimed or emulated.
    w = max(half_width, EPS_WIDTH)
    return smoothstep(center - w, center + w, x)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x * s for x in a)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalize(a):
    l2 = dot(a, a)
    assert l2 > 1e-20
    return scale(a, 1.0 / math.sqrt(l2))


def rotate(a, axis, angle):
    axis = normalize(axis)
    c, s = math.cos(angle), math.sin(angle)
    return add(add(scale(a, c), scale(cross(axis, a), s)),
               scale(axis, dot(axis, a) * (1.0 - c)))


def shape(theta, beta, softness):
    delta = 0.95 * softness * min(beta, PI / 2.0 - beta)
    return 1.0 - band(theta, beta, delta)


def hair_angle(ht, hb, anisotropy):
    c = 1.0 - clamp(anisotropy)
    return math.asin(math.sqrt(clamp(ht * ht + c * c * hb * hb)))


def shifted_frame(shift):
    # N=(0,0,1), T=(1,0,0), B=(0,1,0).
    sn, cs = math.sin(shift), math.cos(shift)
    return (cs, 0.0, sn), (0.0, 1.0, 0.0), (-sn, 0.0, cs)


def field(u, v):
    # Smooth synthetic, asymmetric threshold field; mirror samples differ.
    return 0.12 + 0.70 * (0.60 * u + 0.40 * v)


def face_lit(light, basis, uv, bias=0.0, half_width=2.0 / 180.0):
    # Exactly the report's scalar formula: normalize the horizontal side coordinate.
    f, r, u = (dot(light, axis) for axis in basis)
    horizontal = math.sqrt(max(0.0, f * f + r * r))
    q = math.atan2(abs(r), f) / PI if horizontal > 1e-6 else 0.0
    eta = smoothstep(0.05, 0.15, horizontal)
    ss = math.sin(2.0 * RAD)
    side = smoothstep(-ss, ss, r / max(horizontal, 1e-6))
    ap, am = field(*uv), field(1.0 - uv[0], uv[1])
    dp, dm = band(ap - q + bias, 0.0, half_width), band(am - q + bias, 0.0, half_width)
    lit = dm * (1.0 - side) + dp * side
    front = 1.0 - smoothstep(0.0, 5.0 / 180.0, q)
    back = smoothstep(1.0 - 10.0 / 180.0, 1.0, q)
    lit = (lit * (1.0 - front) + front) * (1.0 - back)
    pole = band(u, 0.15, 0.25)
    return pole * (1.0 - eta) + lit * eta


def direction(yaw, vertical=0.0):
    h = math.sqrt(max(0.0, 1.0 - vertical * vertical))
    return (h * math.cos(yaw), h * math.sin(yaw), vertical)


def require(checks, name, measured, limit, relation="<=", note=""):
    passed = measured <= limit if relation == "<=" else measured >= limit
    checks.append({"name": name, "measured": measured, "relation": relation,
                   "acceptance_limit": limit, "passed": passed, "note": note})


def main():
    rng = random.Random(SEED)
    checks = []
    details = {}

    # GGX HWHM: algebraic D(beta)/D(0) checked independently by direct NDF ratio.
    critical = 2.0 ** (-0.25)
    ggx_rows, max_ratio_error = [], 0.0
    for alpha in (0.01, 0.05, 0.10, 0.30, 0.50, 0.70, 0.83):
        c2 = (1.0 - math.sqrt(2.0) * alpha * alpha) / (1.0 - alpha * alpha)
        beta = math.acos(math.sqrt(c2))
        den = math.sin(beta) ** 2 + alpha * alpha * math.cos(beta) ** 2
        ratio = alpha ** 4 / (den * den)
        err = abs(ratio - 0.5)
        max_ratio_error = max(max_ratio_error, err)
        ggx_rows.append({"alpha": alpha, "beta_degrees": beta / RAD,
                         "NDF_ratio": ratio, "absolute_half_peak_error": err})
    require(checks, "GGX_valid_HWHM_NDF_ratio_error", max_ratio_error, 1e-10)
    boundary_err = abs(critical ** 4 - 0.5)
    require(checks, "GGX_critical_alpha_grazing_half_peak", boundary_err, 1e-14,
            note="Equality alpha=2^(-1/4) has beta=90 degrees at the hemisphere boundary.")
    above = [{"alpha": a, "minimum_hemisphere_ratio": a ** 4} for a in (0.85, 0.90, 0.99, 1.0)]
    require(checks, "GGX_above_critical_no_half_peak_min_ratio",
            min(x["minimum_hemisphere_ratio"] for x in above), 0.5, relation=">=",
            note="For 0<alpha<=1, the NDF ratio is minimal at grazing; alpha=1 is constant.")
    details["ggx"] = {"critical_alpha": critical, "valid_domain_samples": ggx_rows,
                      "above_critical_samples": above,
                      "boundary_beta_degrees": 90.0,
                      "limitation": "Tests NDF shape only, not full BRDF including Fresnel and Smith G."}

    # Angle-space midpoint and demonstrate why cosine-space smoothing drifts.
    midpoint_error = 0.0
    cosine_drifts = []
    for beta_deg in (1, 5, 8, 18, 25, 35, 60):
        beta = beta_deg * RAD
        for soft in (0, 0.01, 0.2, 0.6, 1.0):
            midpoint_error = max(midpoint_error, abs(shape(beta, beta, soft) - 0.5))
            delta = 0.95 * soft * min(beta, PI / 2.0 - beta)
            wrong_mid = math.acos(clamp(math.cos(beta) * math.cos(delta), -1.0, 1.0))
            cosine_drifts.append({"beta_degrees": beta_deg, "softness": soft,
                                  "cosine_smoothstep_midpoint_degrees": wrong_mid / RAD,
                                  "midpoint_drift_degrees": (wrong_mid - beta) / RAD})
    require(checks, "angle_specular_reference_midpoint_error", midpoint_error, 1e-12)
    details["specular"] = {"max_angle_midpoint_error": midpoint_error,
                           "cosine_domain_drift_example": max(cosine_drifts, key=lambda r: r["midpoint_drift_degrees"]),
                           "limitation": "Raw lobe only; intensity, gates, Fresnel, projection and pixel AA are excluded."}

    # Hair exact anisotropy endpoints throughout the shifted-normal front hemisphere.
    iso_error = band_error = 0.0
    endpoint_count = 0
    for ti in range(151):
        theta = (PI / 2.0 - 1e-5) * ti / 150.0
        for ai in range(64):
            az = 2.0 * PI * ai / 64.0
            ht, hb = math.sin(theta) * math.cos(az), math.sin(theta) * math.sin(az)
            iso_error = max(iso_error, abs(hair_angle(ht, hb, 0.0) - theta))
            band_error = max(band_error, abs(hair_angle(ht, hb, 1.0) - math.asin(abs(ht))))
            endpoint_count += 1
    require(checks, "hair_a0_isotropic_angular_identity_error_rad", iso_error, 2e-10)
    require(checks, "hair_a1_KK_inspired_band_identity_error_rad", band_error, 1e-14)

    # Shared primary-section midpoint including nonzero shift and valid front-facing gates.
    hair_midpoint_error = max_frame_error = 0.0
    primary_count = 0
    for shift_deg in (-20, -7, 0, 13, 20):
        ts, bs, ns = shifted_frame(shift_deg * RAD)
        max_frame_error = max(max_frame_error, abs(dot(ts, ns)), abs(dot(ts, bs)),
                              abs(dot(ns, bs)), abs(dot(ts, ts) - 1.0), abs(dot(ns, ns) - 1.0))
        for beta_deg in (1, 8, 18, 25, 60):
            beta = beta_deg * RAD
            h = add(scale(ns, math.cos(beta)), scale(ts, math.sin(beta)))
            for anisotropy in (0.0, 0.25, 0.65, 0.9, 1.0):
                theta = hair_angle(dot(h, ts), dot(h, bs), anisotropy)
                for soft in (0.2, 0.6, 1.0):
                    hair_midpoint_error = max(hair_midpoint_error, abs(shape(theta, beta, soft) - 0.5))
                    primary_count += 1
    require(checks, "hair_shifted_frame_orthonormal_error", max_frame_error, 1e-14)
    require(checks, "hair_primary_section_midpoint_error", hair_midpoint_error, 1e-11)

    # Increasing a must not shrink the RAW lobe response at a fixed H.
    negative_growth = 0.0
    monotonic_samples = 0
    for _ in range(1500):
        theta, az = rng.random() * PI / 2.0, rng.random() * PI * 2.0
        ht, hb = math.sin(theta) * math.cos(az), math.sin(theta) * math.sin(az)
        beta, soft = rng.uniform(1, 60) * RAD, rng.random()
        previous = -1.0
        for i in range(51):
            response = shape(hair_angle(ht, hb, i / 50.0), beta, soft)
            if previous >= 0.0:
                negative_growth = max(negative_growth, previous - response)
            previous = response
            monotonic_samples += 1
    require(checks, "hair_anisotropy_max_negative_raw_response_growth", negative_growth, 1e-13)

    cross_error, cross_count = 0.0, 0
    for beta_deg in (1, 8, 18, 25, 60):
        beta = beta_deg * RAD
        for a in (0.0, 0.1, 0.25, 0.5, 0.75, 0.9):
            c = 1.0 - a
            if c >= math.sin(beta):
                beta_b = math.asin(clamp(math.sin(beta) / c))
                measured = hair_angle(0.0, math.sin(beta_b), a)
                cross_error = max(cross_error, abs(measured - beta))
                cross_count += 1
    require(checks, "hair_cross_section_finite_half_width_angle_error_rad", cross_error, 1e-13)
    details["hair"] = {"endpoint_direction_samples": endpoint_count,
                       "primary_section_samples": primary_count,
                       "monotonic_response_samples": monotonic_samples,
                       "finite_cross_section_samples": cross_count,
                       "max_a0_error_radians": iso_error,
                       "max_a1_error_radians": band_error,
                       "limitation": "Half-width identities refer to raw lobe; final half-hemisphere gates can truncate it. No pixel fwidth was simulated."}

    # Face: full-yaw closure over many UV, bias, softness and elevation conditions.
    basis = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    max_closure = max_front_back_error = 0.0
    closure_count = 0
    for u in (0.05, 0.25, 0.5, 0.75, 0.95):
        for v in (0.1, 0.5, 0.9):
            for vertical in (0.0, 0.5, -0.5, 0.995, -0.995):
                for bias in (-5.0 / 180.0, 0.0, 5.0 / 180.0):
                    for w in (0.0, 2.0 / 180.0, 10.0 / 180.0):
                        start = face_lit(direction(0.0, vertical), basis, (u, v), bias, w)
                        end = face_lit(direction(2.0 * PI, vertical), basis, (u, v), bias, w)
                        max_closure = max(max_closure, abs(start - end))
                        if vertical == 0.0:
                            back = face_lit(direction(PI), basis, (u, v), bias, w)
                            max_front_back_error = max(max_front_back_error, abs(start - 1.0), abs(back))
                        closure_count += 1
    require(checks, "face_full_yaw_periodic_closure_error", max_closure, 1e-12)
    require(checks, "face_horizontal_front_back_endpoint_error", max_front_back_error, 1e-12)

    # Same rotation applied to light and head basis must preserve projected coordinates.
    rotation_error = 0.0
    for _ in range(1000):
        light = normalize(tuple(rng.uniform(-1.0, 1.0) for _ in range(3)))
        axis = normalize(tuple(rng.uniform(-1.0, 1.0) for _ in range(3)))
        angle = rng.uniform(-PI, PI)
        uv = (rng.random(), rng.random())
        before = face_lit(light, basis, uv)
        after = face_lit(rotate(light, axis, angle), tuple(rotate(e, axis, angle) for e in basis), uv)
        rotation_error = max(rotation_error, abs(before - after))
    require(checks, "face_same_rotation_invariance_error", rotation_error, 1e-11)

    # Pole fade must eliminate azimuth and approach a single deterministic value.
    pole_rows, pole_exact_error = [], 0.0
    for sign in (-1.0, 1.0):
        endpoint = face_lit((0.0, 0.0, sign), basis, (0.17, 0.43))
        for h in (0.20, 0.15, 0.10, 0.051, 0.050, 0.049, 0.01, 1e-6, 0.0):
            v = sign * math.sqrt(max(0.0, 1.0 - h * h))
            values = [face_lit((h * math.cos(i * PI / 24.0), h * math.sin(i * PI / 24.0), v),
                               basis, (0.17, 0.43)) for i in range(48)]
            spread = max(values) - min(values)
            endpoint_deviation = max(abs(x - endpoint) for x in values)
            pole_rows.append({"pole_sign": int(sign), "horizontal_length": h,
                              "azimuth_spread": spread, "max_endpoint_deviation": endpoint_deviation})
            if h <= 0.05:
                pole_exact_error = max(pole_exact_error, spread, endpoint_deviation)
    require(checks, "face_pole_azimuth_and_endpoint_error_inside_fallback_zone", pole_exact_error, 1e-12)

    boundary_rows = []
    for boundary in (0.05, 0.15):
        for step in (1e-3, 1e-4, 1e-5, 1e-6):
            jump = 0.0
            for sign in (-1.0, 1.0):
                for i in range(48):
                    az = i * PI / 24.0
                    outputs = []
                    for h in (boundary - step, boundary + step):
                        v = sign * math.sqrt(max(0.0, 1.0 - h * h))
                        outputs.append(face_lit((h * math.cos(az), h * math.sin(az), v), basis, (0.17, 0.43)))
                    jump = max(jump, abs(outputs[1] - outputs[0]))
            boundary_rows.append({"horizontal_fade_boundary": boundary, "symmetric_step": step,
                                  "max_response_difference": jump})
    finest_boundary = max(r["max_response_difference"] for r in boundary_rows if r["symmetric_step"] == 1e-6)
    require(checks, "face_pole_fade_boundary_small_step_response_difference", finest_boundary, 1e-7,
            note="Finite sampling supports continuity; analytical smoothstep composition supplies the proof.")

    yaw_rows = []
    for steps in (720, 1440, 2880, 5760):
        vals = [face_lit(direction(i * 2.0 * PI / steps), basis, (0.17, 0.43), half_width=0.02)
                for i in range(steps + 1)]
        yaw_rows.append({"full_yaw_steps": steps,
                         "max_adjacent_response_change": max(abs(b-a) for a, b in zip(vals, vals[1:]))})
    require(checks, "face_yaw_refinement_max_step_ratio",
            yaw_rows[-1]["max_adjacent_response_change"] / yaw_rows[0]["max_adjacent_response_change"], 0.15,
            note="For a fixed nonzero 0.02 threshold-domain half-width, eightfold angular refinement reduces step change.")
    details["face"] = {"closure_conditions": closure_count, "same_rotation_trials": 1000,
                       "synthetic_field": "A(u,v)=0.12+0.70*(0.60*u+0.40*v), A_minus=A(1-u,v)",
                       "side_blend": "smoothstep(-sin(2deg),sin(2deg),lr/max(h,1e-6)), matching final report",
                       "pole_sweep": pole_rows, "pole_boundary_refinement": boundary_rows,
                       "horizontal_yaw_refinement": yaw_rows,
                       "limitation": "No real Face asset, UV seams, texture compression/mips, head-bone runtime update, UE shadows or temporal AA were tested."}

    # Explicit counterexample for the report's shadow composition wording.
    dlit, visibility = 0.5, 0.5
    details["review_counterexamples"] = {
        "product_vs_min_visibility": {"Dface": dlit, "visibility": visibility,
                                      "product_lit": dlit * visibility, "min_lit": min(dlit, visibility),
                                      "product_shadow": 1.0 - dlit * visibility,
                                      "min_shadow": 1.0 - min(dlit, visibility),
                                      "conclusion": "For soft masks, product visibility is <= min, so product makes the combined shadow stronger; for binary masks they agree."},
        "side_blend_pitch_dependence": [{"horizontal_length": h,
                                         "effective_yaw_half_width_degrees": math.asin(math.sin(2.0 * RAD) / h) / RAD}
                                        for h in (1.0, 0.5, 0.15)],
        "half_hemisphere_filter": {"Band_at_zero_with_zero_art_width": band(0.0, 0.0, 0.0),
                                   "note": "Centered filtered gates are 0.5 at the boundary; raw-lobe width identities must not be attributed unchanged to the final gated response."}
    }

    passed = all(c["passed"] for c in checks)
    result = {"title": "Principled NPR 数学参考模型数值验证",
              "report_date": "2026-10-04", "schema_version": 1,
              "status": "PASS" if passed else "FAIL",
              "scope": "Python 标准库双精度标量模型的可复现数值抽查；不是 UE / HLSL / GPU / 视觉或感知实验。",
              "not_tested": ["UE 材质编译", "真实动态阴影", "GPU 性能", "float32/half 数值误差", "纹理采样与压缩", "fwidth/AA/TSR", "感知正交性", "受控角色资产"],
              "reproduction": {"command": "python Tools/verify_reference_math.py", "seed": SEED,
                               "python_version_used": sys.version.split()[0],
                               "dependencies": "Python standard library only",
                               "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
              "checks_passed": sum(c["passed"] for c in checks), "checks_total": len(checks),
              "checks": checks, "details": details}
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "passed": result["checks_passed"],
                      "total": len(checks), "output": str(OUT),
                      "failed_checks": [c for c in checks if not c["passed"]]}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
