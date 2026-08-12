// renders the plugin editor to a PNG without needing screen-recording access
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    CC2290Processor proc;
    proc.prepareToPlay (44100.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("ui.png"));
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::cout << (ok ? "wrote " : "FAILED ") << out.getFullPathName() << std::endl;
    return ok ? 0 : 1;
}
