#include "SongbirdEditor.h"

//==============================================================================
// Bridge: Settings, API keys, plugin catalog, audio device config
//==============================================================================

void SongbirdEditor::registerSettingsBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        .withNativeFunction("getAvailablePlugins", [this](const juce::Array<juce::var>&, std::function<void(juce::var)> complete) {
            juce::Array<juce::var> instruments;
            
            juce::StringArray validSynths = {
                "Augmented Strings", "Buchla Easel V", "CS-80 V4", "DX7 V",
                "Jun-6 V", "Jup-8 V4", "Mini V3", "OB-Xa V", "Prophet-5 V",
                "Heartbeat"
            };
            
            // List all .vst3 bundles in the standard directory
            juce::File vst3Dir("/Library/Audio/Plug-Ins/VST3");
            if (vst3Dir.isDirectory()) {
                auto files = vst3Dir.findChildFiles(
                    juce::File::findDirectories, false, "*.vst3");
                files.sort();
                
                for (auto& f : files) {
                    auto name = f.getFileNameWithoutExtension();
                    
                    // Filter: only show if in our valid synth list
                    // (prevents effects from cluttering the instrument dropdown)
                    if (!validSynths.contains(name)) continue;
                    
                    juce::DynamicObject* obj = new juce::DynamicObject();
                    obj->setProperty("id", f.getFullPathName());
                    obj->setProperty("name", name);
                    obj->setProperty("vendor", "");
                    obj->setProperty("category", "instrument");
                    instruments.add(juce::var(obj));
                }
            }
            
            // Add Kick 2 (AudioUnit only)
            juce::File kick2AU("/Library/Audio/Plug-Ins/Components/Kick 2.component");
            if (kick2AU.exists()) {
                juce::DynamicObject* obj = new juce::DynamicObject();
                obj->setProperty("id", kick2AU.getFullPathName());
                obj->setProperty("name", "Kick 2");
                obj->setProperty("vendor", "Sonic Academy");
                obj->setProperty("category", "instrument");
                instruments.add(juce::var(obj));
            }
            
            // Curated channel strip / effects list
            juce::Array<juce::var> channelStrips;
            juce::StringArray stripNames = {
                "Console 1", "American Class A", "British Class A",
                "Weiss DS1-MK3", "Summit Audio Grand Channel"
            };
            for (auto& name : stripNames) {
                // Try VST3 first, then AU
                juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
                juce::File au("/Library/Audio/Plug-Ins/Components/" + name + ".component");
                juce::String path = vst3.exists() ? vst3.getFullPathName()
                                  : au.exists()   ? au.getFullPathName()
                                  : "";
                if (path.isNotEmpty()) {
                    juce::DynamicObject* obj = new juce::DynamicObject();
                    obj->setProperty("id", path);
                    obj->setProperty("name", name);
                    obj->setProperty("vendor", "");
                    obj->setProperty("category", "channel-strip");
                    channelStrips.add(juce::var(obj));
                }
            }

            // Curated FX list
            juce::Array<juce::var> fxPlugins;
            juce::StringArray fxNames = {
                "Tube Delay", "ValhallaRoom", "Widener", "soothe2", "Dist TUBE-CULTURE"
            };
            for (auto& name : fxNames) {
                juce::File vst3("/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
                juce::File au("/Library/Audio/Plug-Ins/Components/" + name + ".component");
                juce::String path = vst3.exists() ? vst3.getFullPathName()
                                  : au.exists()   ? au.getFullPathName()
                                  : "";
                // Sometimes Valhalla or others might be in a vendor subfolder for VST3, 
                // but the scanner just needs an ID to tell the UI. 
                // We'll use the path if found, or just the name as ID if we have to, 
                // though Tracktion Engine scan will find it.
                // Let's just output it anyway so the UI knows it's available.
                juce::DynamicObject* obj = new juce::DynamicObject();
                obj->setProperty("id", path.isNotEmpty() ? path : name);
                obj->setProperty("name", name);
                obj->setProperty("vendor", "");
                obj->setProperty("category", "fx");
                fxPlugins.add(juce::var(obj));
            }
            
            juce::DynamicObject* result = new juce::DynamicObject();
            result->setProperty("instruments", instruments);
            result->setProperty("effects", channelStrips);
            result->setProperty("fx", fxPlugins);
            complete(juce::JSON::toString(juce::var(result)));
        })
        // Get Gemini API key from application settings
        .withNativeFunction("getApiKey", [](auto&, auto complete) {
            // Helper to create Songbird-specific PropertiesFile options
            juce::PropertiesFile::Options opts;
            opts.applicationName = "Songbird Player";
            opts.filenameSuffix = ".settings";
            opts.osxLibrarySubFolder = "Application Support";
            opts.folderName = juce::String("Songbird") +
                              juce::File::getSeparatorString() +
                              juce::String("Songbird Player");
            opts.storageFormat = juce::PropertiesFile::storeAsXML;

            juce::ApplicationProperties appProps;
            appProps.setStorageParameters(opts);
            
            auto* settings = appProps.getUserSettings();
            DBG("ApiKey: Loading from " + settings->getFile().getFullPathName());
            
            juce::String key = settings->getValue("gemini-api-key", juce::String());
            DBG("ApiKey: Found key? " + juce::String(key.isNotEmpty() ? "Yes" : "No"));

            complete(key);
        })
        // Save Gemini API key to application settings
        .withNativeFunction("setApiKey", [](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String key = args[0].toString();

                juce::PropertiesFile::Options opts;
                opts.applicationName = "Songbird Player";
                opts.filenameSuffix = ".settings";
                opts.osxLibrarySubFolder = "Application Support";
                opts.folderName = juce::String("Songbird") +
                                  juce::File::getSeparatorString() +
                                  juce::String("Songbird Player");
                opts.storageFormat = juce::PropertiesFile::storeAsXML;

                juce::ApplicationProperties appProps;
                appProps.setStorageParameters(opts);
                appProps.getUserSettings()->setValue("gemini-api-key", key);
                appProps.saveIfNeeded();
                DBG("ApiKey: saved to Songbird Player settings");
            }
            complete("ok");
        });
}
