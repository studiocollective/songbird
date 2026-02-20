#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// WebView native function bridge
//==============================================================================

juce::WebBrowserComponent::Options SongbirdEditor::createWebViewOptions()
{
    return juce::WebBrowserComponent::Options{}
        // Load state from C++ cache (Zustand persist getItem)
        .withNativeFunction("loadState", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String storeName = args[0].toString();
                if (stateCache.count(storeName))
                    complete(stateCache[storeName]);
                else
                    complete("{\"state\":null}");
            }
        })
        // Update state on C++ side (Zustand persist setItem)
        .withNativeFunction("updateState", [this](auto& args, auto complete) {
            if (args.size() > 1) {
                juce::String storeName = args[0].toString();
                juce::String value = args[1].toString();
                handleStateUpdate(storeName, value);
                complete("ok");
            }
        })
        // Reset state (Zustand persist removeItem)
        .withNativeFunction("resetState", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String storeName = args[0].toString();
                stateCache.erase(storeName);
                complete("ok");
            }
        })
        // Open a plugin's editor window
        .withNativeFunction("openPlugin", [this](auto& args, auto complete) {
            logToJS("Bridge: openPlugin called from JS with " + juce::String(args.size()) + " args");
            if (args.size() > 2) {
                int trackId = static_cast<int>(args[0]);
                juce::String slotType = args[1].toString();
                juce::String pluginId = args[2].toString();
                
                logToJS("Bridge: scheduling openPluginWindow(" + juce::String(trackId) + ", " + slotType + ", " + pluginId + ")");
                
                juce::MessageManager::callAsync([this, trackId, slotType, pluginId]() {
                    logToJS("Bridge: async executing openPluginWindow");
                    openPluginWindow(trackId, slotType, pluginId);
                });
            } else {
                logToJS("Bridge: openPlugin called with insufficient args");
            }
            complete("ok");
        })
        // Change plugin on a track (swap instrument or channel strip)
        .withNativeFunction("changePlugin", [this](auto& args, auto complete) {
            logToJS("Bridge: changePlugin called from JS with " + juce::String(args.size()) + " args");
            if (args.size() > 2) {
                int trackId = static_cast<int>(args[0]);
                juce::String slotType = args[1].toString();
                juce::String pluginName = args[2].toString();
                
                logToJS("Bridge: scheduling changePlugin(" + juce::String(trackId) + ", " + slotType + ", '" + pluginName + "')");
                
                juce::MessageManager::callAsync([this, trackId, slotType, pluginName]() {
                    changePlugin(trackId, slotType, pluginName);
                });
            } else {
                logToJS("Bridge: changePlugin called with insufficient args");
            }
            complete("ok");
        })
        // Add a generated track
        .withNativeFunction("addGeneratedTrack", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this]() {
                addGeneratedTrack();
            });
            complete("ok");
        })
        // Remove a generated track
        .withNativeFunction("removeGeneratedTrack", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                int trackId = static_cast<int>(args[0]);
                juce::MessageManager::callAsync([this, trackId]() {
                    removeGeneratedTrack(trackId);
                });
            }
            complete("ok");
        })
        // Query system theme
        .withNativeFunction("getSystemTheme", [](auto&, auto complete) {
            bool isDark = juce::Desktop::getInstance().isDarkModeActive();
            complete(isDark ? "dark" : "light");
        })
        // Set WebView page zoom
        .withNativeFunction("setZoom", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                double zoom = static_cast<double>(args[0]);
                zoomLevel = zoom;
                juce::MessageManager::callAsync([this, zoom]() {
                    #if JUCE_MAC
                    if (webView)
                        setWebViewPageZoom(webView.get(), zoom);
                    #endif
                });
            }
            complete("ok");
        })
        // Get track notes JSON for the UI
        .withNativeFunction("getTrackNotes", [this](auto&, auto complete) {
            complete(getTrackNotesJSON());
        })
        // Read current .bird file content (for AI chat context)
        .withNativeFunction("readBird", [this](auto&, auto complete) {
            if (currentBirdFile.existsAsFile())
                complete(currentBirdFile.loadFileAsString());
            else
                complete("");
        })
        // Load a .bird file
        .withNativeFunction("loadBird", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String path = args[0].toString();
                juce::MessageManager::callAsync([this, path]() {
                    auto file = juce::File(path);
                    currentBirdFile = file;
                    loadBirdFile(file);
                    if (webView) {
                        auto json = getTrackNotesJSON();
                        webView->emitEventIfBrowserIsVisible("trackNotes", juce::var(json));
                    }
                });
            }
            complete("ok");
        })
        // Update current .bird file in-place (from AI tool call)
        .withNativeFunction("updateBird", [this](auto& args, auto complete) {
            if (args.size() > 0 && currentBirdFile.existsAsFile()) {
                juce::String content = args[0].toString();
                juce::MessageManager::callAsync([this, content]() {
                    currentBirdFile.replaceWithText(content);
                    DBG("BirdUpdate: Updated " + currentBirdFile.getFullPathName());
                    loadBirdFile(currentBirdFile);
                });
            }
            complete("ok");
        })
        // Save a .bird file (from AI generation)
        .withNativeFunction("saveBird", [this](auto& args, auto complete) {
            if (args.size() > 1) {
                juce::String relPath = args[0].toString();
                juce::String content = args[1].toString();
                
                // Resolve path relative to app location similar to constructor
                auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
                auto searchDir = appFile.getParentDirectory();
                juce::File targetFile;
                
                // Try to find the "files" directory
                juce::File filesDir;
                for (int i = 0; i < 8; i++) {
                    auto candidate = searchDir.getChildFile("files");
                    if (candidate.isDirectory()) {
                        filesDir = candidate;
                        break;
                    }
                    searchDir = searchDir.getParentDirectory();
                }
                
                if (filesDir.isDirectory()) {
                    targetFile = filesDir.getChildFile(relPath);
                } else {
                    // Fallback to CWD
                    targetFile = juce::File::getCurrentWorkingDirectory().getChildFile(relPath);
                }

                juce::MessageManager::callAsync([this, targetFile, content]() {
                    if (targetFile.existsAsFile()) {
                        targetFile.replaceWithText(content);
                        DBG("BirdSave: Updated " + targetFile.getFullPathName());
                    } else {
                        // Create if not exists (including parent dirs if needed)
                        targetFile.create();
                        targetFile.replaceWithText(content);
                        DBG("BirdSave: Created " + targetFile.getFullPathName());
                    }
                    
                    // Hot-reload: immediately load the updated file into the engine
                    currentBirdFile = targetFile;
                    loadBirdFile(targetFile);
                });
            }
            complete("ok");
        })
        // Get list of available plugins — instruments from filesystem, effects curated
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
