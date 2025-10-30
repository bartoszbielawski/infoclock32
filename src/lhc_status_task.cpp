

#include <pgmspace.h>
#include <http_utils.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <string_utils.h>
#include <string>

static const char pageUrl[] PROGMEM = "https://alicedcs.web.cern.ch/monitoring/screenshots/rss.xml";

static std::map<std::string, std::string> interesting_fields =
{
    {"LhcPage1", ""},
    {"LhcBeamMode", ""},
    {"BeamEnergy", ""},
    {"LhcMachineMode", ""}
};

void remoteHTMLTags(String& str)
{
    str.replace("<br>", "--");
    str.replace("<br/>", "--");
}

void lhc_status_task(void *parameter)
{
    std::string modeAndEnergyMessage;
    std::string page1Message;

    time_t last_update = 0;

    auto& rmd = ResourceManager<LMDS>::getInstance();
    auto& matrix = rmd.getResourceRef();

    while (true)
    {
        if (difftime(time(nullptr), last_update) > 30)
        {
            String output;
            auto response = HttpUtils::httpGet(pageUrl, output, true);
            if (response != 200)
            {
                Serial.printf("LHCStatus: HTTP GET failed, response: %d\n", response);
                vTaskDelay(60000 / portTICK_PERIOD_MS); // wait a minute before
                continue;
            }      
            
            bool fields_updated = false;

            StringViewStream svs(output);  
            while (svs.available())
            {
                String line = svs.readStringUntil('\n');
                line.trim();
                if (not line.startsWith("<title>"))
                    continue; //line doesn't contain what we want
                
                int colonIndex = line.indexOf(':');
                if (colonIndex == -1)
                    continue; //the line has no colon, skip it too

                //extract title
                String title = line.substring(0, colonIndex);
                title.replace("<title>", "");

                //if the title is one of the interesting fields, extract the value and save back to the map
                if (interesting_fields.find(title.c_str()) != interesting_fields.end())
                {
                    String value = line.substring(colonIndex + 1);
                    remoteHTMLTags(value);
                    value.replace("</title>", "");
                    value.trim();
                    interesting_fields[title.c_str()] = value.c_str();
                    Serial.printf("LHCStatus: %s = %s\n", title.c_str(), value.c_str());
                    fields_updated = true;
                }
            }

            if (fields_updated)
            {
                //create message to be displayed
                char buffer[128];
                snprintf_P(buffer, sizeof(buffer), PSTR("%s: %s @ %s"),
                    interesting_fields["LhcMachineMode"].c_str(),
                    interesting_fields["LhcBeamMode"].c_str(),
                    interesting_fields["BeamEnergy"].c_str());
                
                modeAndEnergyMessage = buffer;
                    
                page1Message = interesting_fields["LhcPage1"];
                last_update = time(nullptr);
            }
        }   //end of update block

        if (not rmd.make_access_request())
        {
            Serial.println("LHCStatus: Failed to get access to display");   
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (not modeAndEnergyMessage.empty())
        {
            scrollMessage(modeAndEnergyMessage, matrix, 50);
        }
        rmd.release_access();

        vTaskDelay(5000 / portTICK_PERIOD_MS);
        if (not rmd.make_access_request())
        {
            Serial.println("LHCStatus: Failed to get access to display");   
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if (not page1Message.empty())
        {
            Serial.printf("LHCStatus: Displaying page 1 message: %s\n", page1Message.c_str());
            scrollMessage(page1Message, matrix, 50);
        }
        rmd.release_access();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}