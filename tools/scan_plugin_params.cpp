/**
 * scan_plugin_params — Dump all automatable parameters from a VST3/AU plugin.
 *
 * Usage (after building):
 *   ./scan_plugin_params "/Library/Audio/Plug-Ins/VST3/Console 1.vst3"
 *   ./scan_plugin_params "/Library/Audio/Plug-Ins/VST3/Console 1.vst3" json
 *
 * Prints to stdout in a table (or JSON with "json" arg).
 */

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

//==============================================================================
struct ParamScanner : public juce::JUCEApplicationBase
{
    const juce::String getApplicationName() override    { return "scan_plugin_params"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override           { return true; }

    void initialise(const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens(commandLine, true);

        if (args.isEmpty())
        {
            juce::ConsoleApplication::fail("Usage: scan_plugin_params <path/to/Plugin.vst3> [json]\n");
            quit();
            return;
        }

        juce::String pluginPath = args[0].unquoted();
        bool asJson = (args.size() > 1 && args[1].equalsIgnoreCase("json"));

        // Set up format manager — VST3 only (AU requires extra frameworks not worth linking here)
        juce::AudioPluginFormatManager fm;
        fm.addFormat(new juce::VST3PluginFormat());

        // Describe the plugin
        juce::OwnedArray<juce::PluginDescription> descs;
        for (int i = 0; i < fm.getNumFormats(); ++i)
        {
            auto* fmt = fm.getFormat(i);
            fmt->findAllTypesForFile(descs, pluginPath);
        }

        if (descs.isEmpty())
        {
            std::cerr << "ERROR: Could not find any plugins at: " << pluginPath << "\n";
            std::cerr << "Make sure the path ends in .vst3 (or .component for AU)\n";
            quit();
            return;
        }

        // Use first descriptor
        auto& desc = *descs[0];
        std::cout << "Plugin:  " << desc.name << "\n";
        std::cout << "Format:  " << desc.pluginFormatName << "\n";
        std::cout << "Vendor:  " << desc.manufacturerName << "\n";
        std::cout << "Version: " << desc.version << "\n\n";

        // Instantiate
        juce::String err;
        auto processor = fm.createPluginInstance(desc, 44100.0, 512, err);
        if (!processor)
        {
            std::cerr << "ERROR: Failed to instantiate plugin: " << err << "\n";
            quit();
            return;
        }

        // Initialise with a basic stereo layout
        processor->setPlayConfigDetails(2, 2, 44100.0, 512);
        processor->prepareToPlay(44100.0, 512);

        auto& params = processor->getParameters();
        int n = params.size();

        if (asJson)
        {
            // JSON output — easier to parse programmatically
            std::cout << "[\n";
            for (int i = 0; i < n; ++i)
            {
                auto* p = params[i];
                auto* ap = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
                std::cout << "  {"
                    << "\"index\":" << i
                    << ",\"id\":\"" << (ap ? ap->paramID.toStdString() : "") << "\""
                    << ",\"name\":\"" << p->getName(256).toStdString() << "\""
                    << ",\"default\":" << p->getDefaultValue()
                    << ",\"numSteps\":" << p->getNumSteps()
                    << ",\"isDiscrete\":" << (p->isDiscrete() ? "true" : "false")
                    << ",\"label\":\"" << p->getLabel().toStdString() << "\""
                    << "}" << (i < n-1 ? "," : "") << "\n";
            }
            std::cout << "]\n";
        }
        else
        {
            // Pretty table output
            printf("%-4s  %-40s  %-20s  %s\n", "IDX", "NAME", "ID", "DEFAULT/STEPS");
            printf("%-4s  %-40s  %-20s  %s\n", "---", "----", "--", "----");
            for (int i = 0; i < n; ++i)
            {
                auto* p = params[i];
                auto* ap = dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
                printf("%-4d  %-40s  %-20s  %.3f",
                    i,
                    p->getName(256).toStdString().c_str(),
                    (ap ? ap->paramID.toStdString().c_str() : ""),
                    (double)p->getDefaultValue());
                if (p->isDiscrete())
                    printf("  [%d steps]", p->getNumSteps());
                printf("\n");
            }
            std::cout << "\nTotal: " << n << " parameters\n";
        }

        quit();
    }

    void shutdown()  override {}
    void anotherInstanceStarted(const juce::String&) override {}
    void systemRequestedQuit() override { quit(); }
    void suspended() override {}
    void resumed() override {}
    void unhandledException(const std::exception*, const juce::String&, int) override {}
};

START_JUCE_APPLICATION(ParamScanner)
