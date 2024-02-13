#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/time/real_time_clock.h"

namespace esphome
{
  namespace rika_gsm_mod
  {
    class RikaGSMClimatePollingComponent : public climate::Climate, public PollingComponent, public uart::UARTDevice
    {
    public:
      RikaGSMClimatePollingComponent() : PollingComponent(70000)
      {
        this->traits_.set_supports_action(true);
        this->traits_.set_supports_current_temperature(true);
        this->traits_.set_supports_two_point_target_temperature(false);
        this->traits_.set_visual_min_temperature(30.0);
        this->traits_.set_visual_max_temperature(100.0);
        this->traits_.set_visual_temperature_step(5.0);
        this->target_temperature = 80.0;
        this->current_temperature = 0.0;
        this->action = climate::CLIMATE_ACTION_OFF;
      }

      void loop() override;
      void update() override;
      void setTime(time::RealTimeClock *);

      climate::ClimateTraits traits() override;
      climate::ClimateTraits& config_traits();
      void control(const climate::ClimateCall &call) override;

      void dump_config() override;

    protected:
      std::string pinCode{"1234"};
      std::string phoneNumber{"+32479123456"};
      time::RealTimeClock *time;

      climate::ClimateTraits traits_;

      std::string rikaSerialCmdIn{""};
      bool rikaSerialCmdInReady{false};

      std::string rikaSerialSmsIn{""};
      bool rikaSerialSmsInReady{false};

      std::string myCommand{""};
      bool processingRequest{false};

      void resetSerialSmsIn();
      void sendOK();
      void sendReturnChars();
    };
  }
}