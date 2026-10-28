"""The Govee DreamView Ambilight Bridge integration."""
from __future__ import annotations

import logging
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import Event, HomeAssistant
from homeassistant.components.light import (
    DOMAIN as LIGHT_DOMAIN,
    SERVICE_TURN_ON,
    ATTR_RGB_COLOR,
    ATTR_TRANSITION,
)
from homeassistant.const import ATTR_ENTITY_ID

from .const import (
    DOMAIN,
    CONF_LEFT_LIGHT,
    CONF_CENTER_LIGHT,
    CONF_RIGHT_LIGHT,
    CONF_CEILING_LIGHT,
    CONF_TRANSITION,
    DEFAULT_TRANSITION,
    EVENT_AMBILIGHT_COLOR,
)

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up Govee DreamView Ambilight Bridge from a config entry."""
    hass.data.setdefault(DOMAIN, {})

    left_light = entry.data.get(CONF_LEFT_LIGHT)
    center_light = entry.data.get(CONF_CENTER_LIGHT)
    right_light = entry.data.get(CONF_RIGHT_LIGHT)
    ceiling_light = entry.data.get(CONF_CEILING_LIGHT)
    transition = entry.data.get(CONF_TRANSITION, DEFAULT_TRANSITION)

    async def _handle_ambilight_event(event: Event) -> None:
        data = event.data
        virtual_target = data.get("entity_id")
        try:
            r = int(data.get("r", 0))
            g = int(data.get("g", 0))
            b = int(data.get("b", 0))
        except (ValueError, TypeError):
            return

        target_light = None
        if virtual_target == "light.comedor_luces_izquierda":
            target_light = left_light
        elif virtual_target == "light.comedor_luces_centro":
            target_light = center_light
        elif virtual_target == "light.comedor_luces_derecha":
            target_light = right_light
        elif virtual_target == "light.comedor_luces_techo":
            target_light = ceiling_light

        if not target_light:
            return

        service_data = {
            ATTR_ENTITY_ID: target_light,
            ATTR_RGB_COLOR: (r, g, b),
            ATTR_TRANSITION: transition,
        }

        await hass.services.async_call(
            LIGHT_DOMAIN,
            SERVICE_TURN_ON,
            service_data,
            blocking=False,
        )

    remove_listener = hass.bus.async_listen(
        EVENT_AMBILIGHT_COLOR,
        _handle_ambilight_event,
    )

    hass.data[DOMAIN][entry.entry_id] = {
        "remove_listener": remove_listener,
    }

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    entry_data = hass.data[DOMAIN].pop(entry.entry_id, None)
    if entry_data and "remove_listener" in entry_data:
        entry_data["remove_listener"]()
    return True
