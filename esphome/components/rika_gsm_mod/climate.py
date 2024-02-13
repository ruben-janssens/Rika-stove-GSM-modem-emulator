import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, time, climate
from esphome.const import (
    CONF_TIME_ID,
    CONF_ID,
)

DEPENDENCIES = ['uart']

rika_gsm_mod_ns = cg.esphome_ns.namespace('rika_gsm_mod')
RikaGSMClimatePollingComponent = rika_gsm_mod_ns.class_('RikaGSMClimatePollingComponent', cg.PollingComponent, uart.UARTDevice)

CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(RikaGSMClimatePollingComponent),
    cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
}).extend(cv.polling_component_schema("70s")).extend(uart.UART_DEVICE_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    time_ = yield cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.setTime(time_))
    traits = var.config_traits()
    for mode in ["OFF", "HEAT"]:
        cg.add(traits.add_supported_mode(climate.CLIMATE_MODES[mode]))

    # for preset in ["test"]:
    #    cg.add(traits.add_supported_custom_preset(preset))

    yield cg.register_component(var, config)
    yield uart.register_uart_device(var, config)
    yield climate.register_climate(var, config)
