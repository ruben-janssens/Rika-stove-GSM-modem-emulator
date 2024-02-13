#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "rika_gsm_mod.h"

namespace esphome
{
  namespace rika_gsm_mod
  {
    static const char *const TAG = "rika_gsm_mod.climate";
    const uint8_t ASCII_CR = 0x0D;
    const uint8_t ASCII_LF = 0x0A;
    const uint8_t ASCII_SUB = 0x1A;

    // AT Commands
    // Just accept all these
    // AT -> Test command
    const std::string AT = "AT\r";
    // AT&F -> Restore factory settings
    const std::string ATF = "AT&F\r";
    // ATE{0-1} -> Turn ECHO off or on
    const std::string ATE = "ATE";
    // AT+CMGF={0-1} -> Set message format. 0 = "PDU" / 1 = "TEXT"
    const std::string ATCMGF = "AT+CMGF=";
    // AT+CNMI={params} -> https://infocenter.nordicsemi.com/index.jsp?topic=%2Fref_at_commands%2FREF%2Fat_commands%2Ftext_mode%2Fcnmi_read.html
    const std::string ATCNMI = "AT+CNMI=";
    // Do something with these
    // AT+CMGR={number} -> Reads the messsage at index
    const std::string ATCMGR = "AT+CMGR=";
    // AT+CMGS -> Request to send a message
    const std::string ATCMGS = "AT+CMGS=";
    // AT+CMGD={number} -> Deletes the messsage at index
    const std::string ATCMGD = "AT+CMGD=";

    climate::ClimateTraits RikaGSMClimatePollingComponent::traits()
    {
      return traits_;
    }

    climate::ClimateTraits &RikaGSMClimatePollingComponent::config_traits()
    {
      return traits_;
    }

    void RikaGSMClimatePollingComponent::control(const climate::ClimateCall &call)
    {
      if (!this->processingRequest)
      {
        bool has_mode = call.get_mode().has_value();
        if (has_mode)
        {
          this->mode = *call.get_mode();
          switch (this->mode)
          {
          case climate::CLIMATE_MODE_OFF:
            this->action = climate::CLIMATE_ACTION_OFF;
            this->myCommand = "OFF";
            break;
          case climate::CLIMATE_MODE_HEAT:
            this->action = climate::CLIMATE_ACTION_HEATING;
            this->myCommand = "ON";
            break;
          }
        }

        bool has_temp = call.get_target_temperature().has_value();
        if (has_temp)
        {
          this->target_temperature = *call.get_target_temperature();
          this->myCommand = esphome::str_sprintf("M%d", (int)this->target_temperature);
          this->mode = climate::CLIMATE_MODE_HEAT;
          this->action = climate::CLIMATE_ACTION_HEATING;
        }
      }
      this->publish_state();
    }

    void RikaGSMClimatePollingComponent::loop()
    {
      while (this->available())
      {
        uint8_t inChar;
        this->read_byte(&inChar);
        this->rikaSerialCmdIn += inChar;

        if ((inChar == '\n') || (inChar == ASCII_SUB) || (inChar == ASCII_CR))
        {
          this->rikaSerialCmdInReady = true;
          break;
        }
      }

      if (this->rikaSerialCmdInReady)
      {
        if (this->rikaSerialCmdIn == AT ||
            this->rikaSerialCmdIn == ATF ||
            esphome::str_startswith(this->rikaSerialCmdIn, ATE) ||
            esphome::str_startswith(this->rikaSerialCmdIn, ATCMGF) ||
            esphome::str_startswith(this->rikaSerialCmdIn, ATCNMI))
        {
          this->sendOK();
        }
        else if (esphome::str_startswith(this->rikaSerialCmdIn, ATCMGR))
        {
          ESP_LOGW(TAG, "Read command");
          if (this->myCommand != "")
          {
            this->processingRequest = true;
            ESP_LOGW(TAG, "My command: %s", this->myCommand.c_str());

            this->sendReturnChars();
            this->write_str("+CMGR: \"REC UNREAD\",\"");
            this->write_str(this->phoneNumber.c_str());
            this->write_str("\",,\"");
            this->write_str(this->time->now().strftime("%y/%m/%d,%X+0").c_str());
            this->write_str("\"");
            this->sendReturnChars();
            this->write_str(this->pinCode.c_str());
            this->write_str(" ");
            this->write_str(this->myCommand.c_str());
            this->sendReturnChars();
            this->sendReturnChars();
            this->sendOK();
          }
          else
          {
            this->sendReturnChars();
            this->sendOK();
          }
        }
        else if (esphome::str_startswith(this->rikaSerialCmdIn, ATCMGS))
        {
          ESP_LOGW(TAG, "Start response");
          // this->sendReturnChars();
          // this->write_str(">");
          delay_microseconds_safe(500);
          while (!this->rikaSerialSmsInReady)
          {
            if (this->available())
            {
              uint8_t inChar;
              this->read_byte(&inChar);
              this->rikaSerialSmsIn += inChar;
              if ((inChar == '\n') || (inChar == ASCII_SUB) || (inChar == ASCII_CR))
              {
                this->rikaSerialSmsInReady = true;
              }
            }
          }
          this->sendReturnChars();
          this->write_str("+CMGS : 1");
          this->sendReturnChars();
          this->sendOK();

          ESP_LOGW(TAG, "Received: %s", this->rikaSerialSmsIn.c_str());

          if (esphome::str_startswith(this->rikaSerialSmsIn, "STOVE ON"))
          {
            this->action = climate::CLIMATE_ACTION_HEATING;
            this->mode = climate::CLIMATE_MODE_HEAT;

            std::size_t foundManualMode = this->rikaSerialSmsIn.find("MANUAL MODE");
            if (foundManualMode != std::string::npos)
            {
              std::size_t foundPercentage = this->rikaSerialSmsIn.find("%", foundManualMode);
              if (foundPercentage != std::string::npos)
              {
                std::string percentage = this->rikaSerialSmsIn.substr(foundManualMode + 11, foundPercentage - (foundManualMode + 11));
                this->target_temperature = std::stof(percentage);
              }
            }
          }
          else if (esphome::str_startswith(this->rikaSerialSmsIn, "STOVE OFF"))
          {
            this->action = climate::CLIMATE_ACTION_OFF;
            this->mode = climate::CLIMATE_MODE_OFF;
          }

          // Check for room temperature
          std::size_t foundRT = this->rikaSerialSmsIn.find("RT: ");
          if (foundRT != std::string::npos)
          {
            std::string degrees = this->rikaSerialSmsIn.substr(foundRT + 4, 2);
            this->current_temperature = std::stof(degrees);
          }

          this->publish_state();
          this->processingRequest = false;
          this->rikaSerialSmsInReady = false;
          this->rikaSerialSmsIn = "";
        }
        else if (esphome::str_startswith(this->rikaSerialCmdIn, ATCMGD))
        {
          ESP_LOGW(TAG, "Clear command");
          this->myCommand = "";
          this->sendOK();
        }
        this->rikaSerialCmdIn = "";
        this->rikaSerialCmdInReady = false;
      }
    }

    void RikaGSMClimatePollingComponent::update()
    {
      if (!this->processingRequest && this->myCommand == "")
      {
        this->myCommand = "?";
      }
    }

    void RikaGSMClimatePollingComponent::setTime(time::RealTimeClock *time) { this->time = time; }

    void RikaGSMClimatePollingComponent::sendOK()
    {
      this->write_str("OK");
      this->sendReturnChars();
    }

    void RikaGSMClimatePollingComponent::sendReturnChars()
    {
      this->write_byte(ASCII_CR);
      this->write_byte(ASCII_LF);
    }

    void RikaGSMClimatePollingComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Rika GSM Climate PollingComponent");
    }
  }
}