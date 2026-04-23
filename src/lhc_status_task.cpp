

#include <pgmspace.h>
#include <WiFi.h>
#include <http_utils.hpp>
#include <task_registry.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>
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

static const char sentinel_char = '\x07';
static const char sentinel_str[] = "\x07";
static const char double_sentinel_char[] = "\x07\x07";

void removeHTMLTags(String& str)
{
    str.trim();
    String result;
    result.reserve(str.length());
    int i = 0;
    while (i < str.length()) {

        if (str[i] != '<') 
        {
            result += str[i++];
            continue;
        }

        int close = str.indexOf('>', i);
        if (close == -1) break;
        String tag = str.substring(i + 1, close);
        tag.trim();
        if (tag.endsWith("/")) tag.remove(tag.length() - 1);
        tag.trim();
        tag.toUpperCase();
        //replace <br> with a special char that will later be replaced with a space, this way we preserve intentional line breaks
        if (tag == "BR") result += sentinel_char; 
        i = close + 1;        
    }

    result.trim();

    while (result.indexOf(double_sentinel_char) != -1)
        result.replace(double_sentinel_char, sentinel_str);
    
    if (result.startsWith(sentinel_str)) result.remove(0, 1);
    if (result.endsWith(sentinel_str)) result.remove(result.length() - 1, 1);

    //replace remaining sentinel chars (originally <br>) with spaces, adding extra sentinels around them to preserve intentional multiple spaces
    result.replace(sentinel_str, " \x07 "); 
    result.trim();

    // Decode HTML entities (&amp; must be last)
    result.replace("&nbsp;",  " ");
    result.replace("&lt;",    "<");
    result.replace("&gt;",    ">");
    result.replace("&quot;",  "\"");
    result.replace("&apos;",  "'");
    result.replace("&ndash;", "-");
    result.replace("&mdash;", "-");
    result.replace("&amp;",   "&");

    str = result;
}



void lhc_status_task(void *parameter)
{
    registerTask("LHC", 8192);
    std::string modeAndEnergyMessage;
    std::string page1Message;

    time_t last_update = 0;

    auto& rmd = ResourceManager<LMDS>::getInstance();
    auto& matrix = rmd.getResourceRef();

    while (true)
    {
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(30000 / portTICK_PERIOD_MS);
            continue;
        }

        if (difftime(time(nullptr), last_update) > 30)
        {
            String output;
            auto response = HttpUtils::httpGet(pageUrl, output, true);
            if (response != 200)
            {
                logPrintf("LHC", "HTTP GET failed, response: %d", response);
                vTaskDelay(300 * 1000 / portTICK_PERIOD_MS); // wait a minute before
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
                    value.replace("</title>", "");
                    removeHTMLTags(value);                    
                    value.trim();
                    interesting_fields[title.c_str()] = value.c_str();
                    logPrintf("LHC", "%s = %s", title.c_str(), value.c_str());
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
            logPrintf("LHC", "Failed to get access to display");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (not modeAndEnergyMessage.empty())
        {
            scrollMessage(modeAndEnergyMessage, matrix, 20);
        }
        rmd.release_access();

        vTaskDelay(5000 / portTICK_PERIOD_MS);
        if (not rmd.make_access_request())
        {
            logPrintf("LHC", "Failed to get access to display");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if (not page1Message.empty())
        {
            logPrintf("LHC", "Page1: %s", page1Message.c_str());
            scrollMessage(page1Message, matrix, 20);
        }
        rmd.release_access();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}