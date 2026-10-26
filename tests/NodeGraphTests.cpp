#include "PluginEditor.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    void set (PluginProcessor& processor, size_t band, const char* name, float value)
    {
        auto* parameter = processor.parameters.getParameter (openq::bandParameterId (band, name));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    auto settings (PluginProcessor& processor, size_t band)
    {
        return openq::getBandParameterPointers (processor.parameters, band).load();
    }

    juce::Component& node (openq::ResponseGraph& graph, size_t band)
    {
        auto* component = graph.findChildWithID ("node." + juce::String (band));
        REQUIRE (component != nullptr);
        return *component;
    }

    juce::MouseEvent mouse (juce::Component& receiver, juce::Point<float> position, int modifiers = juce::ModifierKeys::leftButtonModifier)
    {
        const auto time = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys (modifiers), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &receiver, &receiver, time, position, time, 1, false };
    }

    struct Gestures : juce::AudioProcessorParameter::Listener
    {
        explicit Gestures (PluginProcessor& p) : processor (p), depth (static_cast<size_t> (p.getParameters().size()))
        {
            for (auto* parameter : processor.getParameters())
                parameter->addListener (this);
        }
        ~Gestures() override
        {
            for (auto* parameter : processor.getParameters())
                parameter->removeListener (this);
        }
        void parameterValueChanged (int index, float) override { changes.push_back (index); }
        void parameterGestureChanged (int index, bool starting) override
        {
            depth[static_cast<size_t> (index)] += starting ? 1 : -1;
            CHECK (depth[static_cast<size_t> (index)] >= 0);
        }
        int active() const
        {
            int sum = 0;
            for (auto value : depth)
                sum += value;
            return sum;
        }
        PluginProcessor& processor;
        std::vector<int> depth;
        std::vector<int> changes;
    };
}

TEST_CASE ("Nodes reflect status, automation, recall and low sample rates without editing settings", "[nodes][ui]")
{
    PluginProcessor processor;
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 280);
    CHECK_FALSE (node (graph, 0).isVisible());
    processor.prepareToPlay (48000.0, 64);
    graph.resized();
    CHECK (node (graph, 0).isVisible());
    CHECK_FALSE (node (graph, 1).isVisible());
    const auto before = node (graph, 0).getBounds().getCentre();
    set (processor, 0, "frequency", 4000.0f);
    set (processor, 0, "gain", 12.0f);
    set (processor, 0, "status", 1.0f);
    graph.setSelectedBand (0);
    CHECK (node (graph, 0).isVisible());
    CHECK (node (graph, 0).getAlpha() < 0.5f);
    CHECK (node (graph, 0).getBounds().getCentreX() > before.x);
    CHECK (node (graph, 0).getBounds().getCentreY() < before.y);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    set (processor, 0, "status", 0.0f);
    graph.setSelectedBand (0);
    CHECK_FALSE (node (graph, 0).isVisible());
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    graph.setSelectedBand (0);
    CHECK (node (graph, 0).isVisible());
    set (processor, 0, "frequency", 20000.0f);
    processor.prepareToPlay (8000.0, 64);
    graph.resized();
    CHECK_THAT (settings (processor, 0).frequency, Catch::Matchers::WithinAbs (20000.0, 0.01));
    CHECK (node (graph, 0).getBounds().getCentreX() < 696);
    const auto image = graph.createComponentSnapshot (graph.getLocalBounds());
    CHECK (image.isValid());
    juce::MemoryOutputStream png;
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, png));
    REQUIRE (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-node-graph.png").replaceWithData (png.getData(), png.getDataSize()));
}

TEST_CASE ("Dragging a newly selected overlapping node edits only its band and balances gestures", "[nodes][ui]")
{
    PluginProcessor processor;
    set (processor, 1, "status", 2.0f);
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor->findChildWithID ("response.graph"));
    REQUIRE (graph != nullptr);
    auto& second = node (*graph, 1);
    const auto center = second.getBounds().getCentre().toFloat();
    second.mouseDown (mouse (second, { 15.0f, 12.0f }));
    CHECK (graph->getSelectedBand() == 1);
    CHECK (graph->getComponentAt (center.roundToInt()) == &second);
    CHECK (gestures.active() == 2);
    auto* frequency = dynamic_cast<juce::Slider*> (editor->findChildWithID ("band.card")->findChildWithID ("band.frequency"));
    REQUIRE (frequency != nullptr);
    CHECK_FALSE (frequency->isEnabled());
    second.mouseDrag (mouse (second, second.getLocalPoint (graph, center + juce::Point<float> (63.0f, -20.0f))));
    CHECK (settings (processor, 1).frequency > 1000.0);
    CHECK (settings (processor, 1).gain > 0.0);
    CHECK_THAT (settings (processor, 0).frequency, Catch::Matchers::WithinAbs (1000.0, 0.001));
    second.mouseUp (mouse (second, { 12.0f, 12.0f }, 0));
    CHECK (gestures.active() == 0);
    CHECK (frequency->isEnabled());
    CHECK_THAT (frequency->getValue(), Catch::Matchers::WithinAbs (settings (processor, 1).frequency, 0.001));
    second.mouseDown (mouse (second, { 12.0f, 12.0f }));
    REQUIRE (gestures.active() == 2);
    editor.reset();
    CHECK (gestures.active() == 0);
}

TEST_CASE ("Node axis locks, fine motion, wheel and boundaries use display mappings", "[nodes][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (8000.0, 64);
    Gestures gestures (processor);
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 280);
    auto& first = node (graph, 0);
    const int locked = juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier;
    const auto pointer = first.getBounds().getCentre().toFloat() + juce::Point<float> (88.0f, -20.0f);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }, locked));
    first.mouseDrag (mouse (first, { 100.0f, -8.0f }, locked));
    CHECK_THAT (settings (processor, 0).frequency, Catch::Matchers::WithinAbs (1000.0, 0.001));
    const auto gain = settings (processor, 0).gain;
    first.mouseDrag (mouse (first, first.getLocalPoint (&graph, pointer)));
    CHECK_THAT (settings (processor, 0).frequency, Catch::Matchers::WithinAbs (1000.0, 0.001));
    CHECK_THAT (settings (processor, 0).gain, Catch::Matchers::WithinAbs (gain, 0.001));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    const auto center = first.getBounds().getCentre().toFloat();
    first.mouseDrag (mouse (first, first.getLocalPoint (&graph, center + juce::Point<float> (100.0f, 0.0f)), juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    CHECK (settings (processor, 0).frequency > 1000.0);
    CHECK (settings (processor, 0).frequency < 1300.0);
    first.mouseDrag (mouse (first, { 10000.0f, -10000.0f }));
    CHECK_THAT (settings (processor, 0).frequency, Catch::Matchers::WithinAbs (3920.0, 0.01));
    CHECK_THAT (settings (processor, 0).gain, Catch::Matchers::WithinAbs (24.0, 0.001));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    juce::MouseWheelDetails wheel;
    wheel.deltaY = 0.25f;
    first.mouseWheelMove (mouse (first, { 12.0f, 12.0f }, 0), wheel);
    CHECK (settings (processor, 0).q > 1.0);
    CHECK (gestures.active() == 0);
}

TEST_CASE ("Node creation initializes before On, bypass retains settings, and deletion reuses slots", "[nodes][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 280);
    set (processor, 1, "q", 9.0f);
    set (processor, 1, "type", 2.0f);
    gestures.changes.clear();
    graph.mouseDoubleClick (mouse (graph, { 500.0f, 80.0f }));
    CHECK (graph.getSelectedBand() == 1);
    CHECK (settings (processor, 1).status == openq::BandStatus::On);
    CHECK (settings (processor, 1).gain > 0.0);
    CHECK_THAT (settings (processor, 1).q, Catch::Matchers::WithinAbs (1.0, 0.001));
    REQUIRE_FALSE (gestures.changes.empty());
    CHECK (gestures.changes.back() == processor.parameters.getParameter ("band.1.status")->getParameterIndex());
    CHECK (settings (processor, 1).type == openq::FilterType::Bell);
    const auto original = settings (processor, 1);
    auto& second = node (graph, 1);
    second.mouseDown (mouse (second, { 12.0f, 12.0f }, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
    CHECK (settings (processor, 1).status == openq::BandStatus::Bypass);
    CHECK (second.isVisible());
    second.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    CHECK (settings (processor, 1) == original);
    second.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK (settings (processor, 1).status == openq::BandStatus::Off);
    CHECK_FALSE (second.isVisible());
    CHECK (graph.getSelectedBand() == 0);
    graph.mouseDoubleClick (mouse (graph, { 300.0f, 160.0f }));
    CHECK (graph.getSelectedBand() == 1);
    CHECK (settings (processor, 1).gain < 0.0);
    for (size_t band = 0; band < openq::bandCount; ++band)
        set (processor, band, "status", 2.0f);
    const auto before = settings (processor, 1);
    graph.mouseDoubleClick (mouse (graph, { 400.0f, 100.0f }));
    CHECK (settings (processor, 1) == before);
    CHECK (gestures.active() == 0);
}

TEST_CASE ("Interrupted node drags end host gestures", "[nodes][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 280);
    auto& first = node (graph, 0);
    for (int interruption = 0; interruption < 4; ++interruption)
    {
        set (processor, 0, "status", 2.0f);
        graph.setSelectedBand (0);
        first.mouseDown (mouse (first, { 12.0f, 12.0f }));
        REQUIRE (gestures.active() == 2);
        if (interruption == 0)
            graph.resized();
        if (interruption == 1)
            graph.setSelectedBand (1);
        if (interruption == 2)
        {
            set (processor, 0, "status", 0.0f);
            graph.setSelectedBand (0);
        }
        if (interruption == 3)
        {
            processor.prepareToPlay (8000.0, 64);
            first.mouseDrag (mouse (first, { 20.0f, 20.0f }));
        }
        CHECK (gestures.active() == 0);
    }
}

TEST_CASE ("Band card opens on selection, preserves settings when hidden and deletes through graph selection", "[nodes][ui][card]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor->findChildWithID ("response.graph"));
    auto* card = editor->findChildWithID ("band.card");
    REQUIRE (graph != nullptr);
    REQUIRE (card != nullptr);
    CHECK_FALSE (card->isVisible());
    CHECK (graph->getSelectedBand() == openq::bandCount);
    auto& first = node (*graph, 0);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    REQUIRE (card->isVisible());
    const auto selectedNode = first.getBounds().translated (graph->getX(), graph->getY());
    CHECK_FALSE (card->getBounds().intersects (selectedNode));
    auto* gain = dynamic_cast<juce::Slider*> (card->findChildWithID ("band.gain"));
    auto* power = dynamic_cast<juce::DrawableButton*> (card->findChildWithID ("band.power"));
    REQUIRE (gain != nullptr);
    REQUIRE (power != nullptr);
    REQUIRE (power->getCurrentImage() != nullptr);
    gain->setValue (6.0, juce::sendNotificationSync);
    power->setToggleState (false, juce::sendNotificationSync);
    const auto saved = settings (processor, 0);
    CHECK (saved.status == openq::BandStatus::Bypass);
    CHECK_THAT (saved.gain, Catch::Matchers::WithinAbs (6.0, 0.001));
    graph->mouseDown (mouse (*graph, { 100.0f, 60.0f }));
    CHECK_FALSE (card->isVisible());
    CHECK (settings (processor, 0) == saved);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    CHECK (card->isVisible());
    CHECK_FALSE (power->getToggleState());
    CHECK_THAT (gain->getValue(), Catch::Matchers::WithinAbs (6.0, 0.001));
    auto* close = dynamic_cast<juce::DrawableButton*> (card->findChildWithID ("band.close"));
    REQUIRE (close != nullptr);
    REQUIRE (close->getCurrentImage() != nullptr);
    for (const float state : { 2.0f, 1.0f })
    {
        set (processor, 0, "status", state);
        const auto beforeClose = settings (processor, 0);
        const auto changes = gestures.changes.size();
        close->onClick();
        CHECK_FALSE (card->isVisible());
        CHECK (first.isVisible());
        CHECK (graph->getSelectedBand() == openq::bandCount);
        CHECK (settings (processor, 0) == beforeClose);
        CHECK (gestures.changes.size() == changes);
        first.mouseDown (mouse (first, { 12.0f, 12.0f }));
        first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
        CHECK (card->isVisible());
        CHECK (power->getToggleState() == (beforeClose.status == openq::BandStatus::On));
    }
    gain->onDragStart();
    const auto cardBounds = card->getBounds();
    set (processor, 0, "gain", -24.0f);
    graph->setSelectedBand (0);
    CHECK (card->getBounds() == cardBounds);
    gain->onDragEnd();
    CHECK_FALSE (card->getBounds().intersects (first.getBounds().translated (graph->getX(), graph->getY())));
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::MemoryOutputStream png;
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, png));
    REQUIRE (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-band-card.png").replaceWithData (png.getData(), png.getDataSize()));
    auto* remove = dynamic_cast<juce::DrawableButton*> (card->findChildWithID ("band.delete"));
    REQUIRE (remove != nullptr);
    REQUIRE (remove->getCurrentImage() != nullptr);
    remove->onClick();
    CHECK (settings (processor, 0).status == openq::BandStatus::Off);
    CHECK_FALSE (card->isVisible());
    CHECK (graph->getSelectedBand() == openq::bandCount);
    CHECK (gestures.active() == 0);
}

TEST_CASE ("Band card retains a clear position after knob release and node motion", "[nodes][ui][card]")
{
    PluginProcessor processor;
    set (processor, 0, "frequency", 200.0f);
    set (processor, 0, "gain", -12.0f);
    processor.prepareToPlay (44100.0, 64);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor->findChildWithID ("response.graph"));
    auto* card = editor->findChildWithID ("band.card");
    REQUIRE (graph != nullptr);
    REQUIRE (card != nullptr);
    auto& selected = node (*graph, 0);
    selected.mouseDown (mouse (selected, { 12.0f, 12.0f }));
    selected.mouseUp (mouse (selected, { 12.0f, 12.0f }, 0));
    const auto rightPosition = card->getBounds();
    REQUIRE (rightPosition.getCentreX() > editor->getWidth() / 2);
    auto* gain = dynamic_cast<juce::Slider*> (card->findChildWithID ("band.gain"));
    REQUIRE (gain != nullptr);
    gain->onDragStart();
    gain->setValue (14.0, juce::sendNotificationSync);
    graph->setSelectedBand (0);
    CHECK (card->getBounds() == rightPosition);
    gain->onDragEnd();
    CHECK (card->getBounds() == rightPosition);

    auto& first = node (*graph, 0);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    first.mouseDrag (mouse (first, { 32.0f, 12.0f }));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    CHECK (card->getBounds() == rightPosition);

    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    const auto blockedPoint = rightPosition.getCentre().toFloat() - graph->getPosition().toFloat();
    first.mouseDrag (mouse (first, first.getLocalPoint (graph, blockedPoint)));
    const auto clearPosition = card->getBounds();
    CHECK (clearPosition != rightPosition);
    CHECK_FALSE (clearPosition.intersects (first.getBounds().translated (graph->getX(), graph->getY()).expanded (12)));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    CHECK (card->getBounds() == clearPosition);

    editor->setSize (820, 640);
    CHECK (graph->getBounds().reduced (18, 44).contains (card->getBounds()));
}

TEST_CASE ("Shelf nodes use half-gain placement and cancel drags on host type changes", "[shelf][nodes][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 280);
    auto& first = node (graph, 0);
    const auto zeroY = first.getBounds().getCentreY();
    set (processor, 0, "gain", 12.0f);
    graph.setSelectedBand (0);
    const auto bellY = first.getBounds().getCentreY();
    for (float type : { 1.0f, 2.0f })
    {
        set (processor, 0, "type", type);
        graph.setSelectedBand (0);
        CHECK_THAT (first.getBounds().getCentreY(), Catch::Matchers::WithinAbs ((bellY + zeroY) * 0.5, 1.0));
        first.mouseDown (mouse (first, { 12.0f, 12.0f }));
        first.mouseDrag (mouse (first, { 12.0f, -10000.0f }));
        CHECK_THAT (settings (processor, 0).gain, Catch::Matchers::WithinAbs (24.0, 0.001));
        first.mouseDrag (mouse (first, { 12.0f, 10000.0f }));
        CHECK_THAT (settings (processor, 0).gain, Catch::Matchers::WithinAbs (-24.0, 0.001));
        REQUIRE (gestures.active() == 2);
        set (processor, 0, "type", 0.0f);
        graph.setSelectedBand (0);
        CHECK (gestures.active() == 0);
        set (processor, 0, "gain", 12.0f);
    }
}

TEST_CASE ("Graph fills the configured bank and reuses the final deleted slot", "[nodes][multiband][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 280);
    for (size_t band = 1; band < openq::bandCount; ++band)
    {
        CAPTURE (band);
        const auto x = 100.0f + 500.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount);
        graph.mouseDoubleClick (mouse (graph, { x, 120.0f }));
        CHECK (graph.getSelectedBand() == band);
        CHECK (settings (processor, band).status == openq::BandStatus::On);
        CHECK (node (graph, band).isVisible());
    }
    std::array<openq::BandSettings, openq::bandCount> fullBank;
    for (size_t band = 0; band < openq::bandCount; ++band)
        fullBank[band] = settings (processor, band);
    graph.mouseDoubleClick (mouse (graph, { 400.0f, 100.0f }));
    for (size_t band = 0; band < openq::bandCount; ++band)
        CHECK (settings (processor, band) == fullBank[band]);
    constexpr auto lastBand = openq::bandCount - 1;
    node (graph, lastBand).keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK_FALSE (node (graph, lastBand).isVisible());
    graph.mouseDoubleClick (mouse (graph, { 300.0f, 160.0f }));
    CHECK (graph.getSelectedBand() == lastBand);
    CHECK (node (graph, lastBand).isVisible());
    CHECK (settings (processor, lastBand).status == openq::BandStatus::On);
    CHECK (gestures.active() == 0);
}
