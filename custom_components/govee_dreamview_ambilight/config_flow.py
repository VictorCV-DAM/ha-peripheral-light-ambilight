"""Config flow for Govee DreamView Ambilight Bridge integration."""
from __future__ import annotations

from typing import Any
import voluptuous as vol

from homeassistant import config_entries
from homeassistant.helpers import selector
import homeassistant.helpers.config_validation as cv

from .const import (
    DOMAIN,
    CONF_LEFT_LIGHT,
    CONF_CENTER_LIGHT,
    CONF_RIGHT_LIGHT,
    CONF_CEILING_LIGHT,
    CONF_TRANSITION,
    DEFAULT_TRANSITION,
)


class GoveeAmbilightConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config flow for Govee DreamView Ambilight Bridge."""

    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        """Handle the initial step."""
        errors: dict[str, str] = {}

        if user_input is not None:
            return self.async_create_entry(
                title="Govee DreamView Ambilight",
                data=user_input,
            )

        data_schema = vol.Schema(
            {
                vol.Required(CONF_LEFT_LIGHT): selector.EntitySelector(
                    selector.EntitySelectorConfig(domain="light")
                ),
                vol.Required(CONF_CENTER_LIGHT): selector.EntitySelector(
                    selector.EntitySelectorConfig(domain="light")
                ),
                vol.Required(CONF_RIGHT_LIGHT): selector.EntitySelector(
                    selector.EntitySelectorConfig(domain="light")
                ),
                vol.Optional(CONF_CEILING_LIGHT): selector.EntitySelector(
                    selector.EntitySelectorConfig(domain="light")
                ),
                vol.Optional(
                    CONF_TRANSITION, default=DEFAULT_TRANSITION
                ): selector.NumberSelector(
                    selector.NumberSelectorConfig(
                        min=0.0,
                        max=1.0,
                        step=0.05,
                        unit_of_measurement="s",
                        mode=selector.NumberSelectorMode.SLIDER,
                    )
                ),
            }
        )

        return self.async_show_form(
            step_id="user",
            data_schema=data_schema,
            errors=errors,
        )
