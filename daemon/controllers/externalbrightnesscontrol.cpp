/*  This file is part of the KDE project
    SPDX-FileCopyrightText: 2024 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "externalbrightnesscontrol.h"
#include "displaybrightness.h"

#include <algorithm>

static constexpr uint32_t s_version = 3;

ExternalBrightnessController::ExternalBrightnessController()
    : QWaylandClientExtensionTemplate<ExternalBrightnessController, &QtWayland::kde_external_brightness_v1::destroy>(s_version)
{
}

void ExternalBrightnessController::setDisplays(const QList<DisplayBrightness *> &displays)
{
    if (!isActive()) {
        m_waylandObjects.clear();
        return;
    }
    std::erase_if(m_waylandObjects, [&displays](const auto &pair) {
        const auto &[display, waylandObj] = pair;
        return !displays.contains(display);
    });
    for (DisplayBrightness *display : displays) {
        if (!m_waylandObjects.contains(display)) {
            m_waylandObjects.emplace(display, std::make_unique<ExternalBrightnessControl>(this, display));
        }
    }
}

ExternalBrightnessControl::ExternalBrightnessControl(ExternalBrightnessController *controller, DisplayBrightness *display)
    : QtWayland::kde_external_brightness_device_v1(controller->create_brightness_control())
    , m_display(display)
{
    set_internal(display->isInternal() ? 1 : 0);
    if (auto data = display->edidData()) {
        set_edid(QString::fromStdString(data->toBase64().toStdString()));
    }
    set_max_brightness(display->maxBrightness());
    if (version() >= 2) {
        set_observed_brightness(m_display->brightness());
    }
    if (version() >= 3) {
        set_uses_ddc_ci(m_display->usesDdcCi() ? 1 : 0);
    }
    commit();
    connect(display, &DisplayBrightness::externalBrightnessChangeObserved, this, [this]() {
        set_max_brightness(m_display->maxBrightness());
        if (version() >= 2) {
            set_observed_brightness(m_display->brightness());
        }
        commit();
    });
}

ExternalBrightnessControl::~ExternalBrightnessControl()
{
    destroy();
}

void ExternalBrightnessControl::kde_external_brightness_device_v1_requested_brightness(uint32_t value)
{
    // The Wayland protocol uses an unsigned 32-bit value, while DDC/CI accepts
    // only the range reported by the monitor. Do not allow a malformed or stale
    // compositor request to wrap during conversion to int and reach the monitor.
    const uint32_t maxBrightness = std::max(0, m_display->maxBrightness());
    const uint32_t boundedValue = std::min(value, maxBrightness);
    if (value != boundedValue) {
        qCWarning(POWERDEVIL) << "Ignoring out-of-range external brightness request" << value << "for" << m_display->label() << "; clamping to" << boundedValue
                              << "/" << maxBrightness;
    }
    m_display->setBrightness(static_cast<int>(boundedValue), false);
}

#include "moc_externalbrightnesscontrol.cpp"
