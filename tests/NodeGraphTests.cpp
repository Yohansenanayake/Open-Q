#include "PluginEditor.h"
#include "ui/UiScale.h"
#include "ui/Palette.h"
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

TEST_CASE ("Band slot colours stay distinct and follow selection without changing parameters", "[nodes][ui][card][colours]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 400);
    openq::BandControlCard card (processor.parameters);
    card.setSize (openq::BandControlCard::preferredWidth, openq::BandControlCard::preferredHeight);
    auto* power = dynamic_cast<juce::DrawableButton*> (card.findChildWithID ("band.power"));
    REQUIRE (power != nullptr);
    const auto containsColour = [] (const juce::Image& image, juce::Colour colour) {
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y) == colour)
                    return true;
        return false;
    };
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        const auto colour = openq::palette::bandColour (band);
        INFO ("Band " << band + 1);
        CHECK (colour.isOpaque());
        for (size_t previous = 0; previous < band; ++previous)
            CHECK (colour != openq::palette::bandColour (previous));
        set (processor, band, "status", static_cast<float> (openq::BandStatus::On));
        const auto before = settings (processor, band);
        graph.setSelectedBand (band);
        card.setBand (band);
        for (const auto* name : { "band.frequency", "band.gain", "band.q" })
        {
            auto* slider = dynamic_cast<juce::Slider*> (card.findChildWithID (name));
            REQUIRE (slider != nullptr);
            CHECK (slider->findColour (juce::Slider::thumbColourId) == colour);
            CHECK (slider->findColour (juce::Slider::rotarySliderFillColourId) == colour);
        }
        CHECK (containsColour (power->createComponentSnapshot (power->getLocalBounds()), colour));
        auto& bandNode = node (graph, band);
        CHECK (containsColour (bandNode.createComponentSnapshot (bandNode.getLocalBounds()), colour));
        CHECK (settings (processor, band) == before);
    }
    // Returning to a slot must recolour the reusable controls back to that slot.
    card.setBand (0);
    const auto first = openq::palette::bandColour (0);
    CHECK (containsColour (power->createComponentSnapshot (power->getLocalBounds()), first));
    set (processor, 0, "frequency", 8000.0f);
    set (processor, 0, "status", static_cast<float> (openq::BandStatus::Bypass));
    card.setBand (0);
    auto* gain = dynamic_cast<juce::Slider*> (card.findChildWithID ("band.gain"));
    REQUIRE (gain != nullptr);
    const auto dimmed = gain->findColour (juce::Slider::thumbColourId);
    CHECK (dimmed != first);
    CHECK (dimmed.getPerceivedBrightness() < first.getPerceivedBrightness());
    CHECK_FALSE (power->getToggleState());
    const auto bypassBorder = card.createComponentSnapshot (card.getLocalBounds()).getPixelAt (0, card.getHeight() / 2);
    const auto bypassSettings = settings (processor, 0);
    CHECK (gain->isEnabled());
    gain->setValue (6.0, juce::sendNotificationSync);
    CHECK_THAT (settings (processor, 0).gain, Catch::Matchers::WithinAbs (6.0, 0.001));
    CHECK (settings (processor, 0).status == bypassSettings.status);
    // Status automation updates the open card without rebinding its controls.
    set (processor, 0, "status", static_cast<float> (openq::BandStatus::On));
    CHECK (gain->findColour (juce::Slider::thumbColourId) == first);
    CHECK (gain->findColour (juce::Slider::rotarySliderFillColourId) == first);
    CHECK (card.createComponentSnapshot (card.getLocalBounds()).getPixelAt (0, card.getHeight() / 2) != bypassBorder);
    set (processor, 0, "status", static_cast<float> (openq::BandStatus::Bypass));
    CHECK (gain->findColour (juce::Slider::thumbColourId) == dimmed);
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

TEST_CASE ("Compact band selectors use editor child popups and preserve parameter gestures", "[nodes][ui][card][popup]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
    editor->setVisible (true);
    auto* card = dynamic_cast<openq::BandControlCard*> (editor->findChildWithID ("band.card"));
    REQUIRE (card != nullptr);
    card->setBand (0);
    card->setBounds (20, 300, openq::BandControlCard::preferredWidth, openq::BandControlCard::preferredHeight);
    auto* type = dynamic_cast<openq::CompactComboBox*> (card->findChildWithID ("band.type"));
    auto* slope = dynamic_cast<openq::CompactComboBox*> (card->findChildWithID ("band.slope"));
    auto* frequency = card->findChildWithID ("band.frequency");
    REQUIRE (type != nullptr);
    REQUIRE (slope != nullptr);
    REQUIRE (frequency != nullptr);
    CHECK (type->getRight() <= frequency->getX());
    CHECK (slope->getRight() <= frequency->getX());
    CHECK (slope->getY() > type->getY());
    juce::Label label;
    const auto options = type->getLookAndFeel().getOptionsForComboBoxPopupMenu (*type, label);
    CHECK (options.getParentComponent() == editor.get());
    CHECK (options.getTargetComponent() == type);
    const auto desktopComponents = juce::Desktop::getInstance().getNumComponents();
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    {
        type->showPopup();
        CHECK_FALSE (type->isPopupActive());
        CHECK (juce::Desktop::getInstance().getNumComponents() == desktopComponents);
        SKIP ("Popup interaction checks require a display; editor parenting and headless rejection were checked.");
    }
    type->showPopup();
    auto* popup = juce::Component::getCurrentlyModalComponent();
    REQUIRE (popup != nullptr);
    CHECK (popup->getParentComponent() == editor.get());
    CHECK_FALSE (popup->isOnDesktop());
    CHECK (juce::Desktop::getInstance().getNumComponents() == desktopComponents);
    // Keyboard selection uses the same JUCE menu result/attachment path as a mouse selection.
    CHECK (popup->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (popup->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (settings (processor, 0).type == openq::FilterType::LowShelf);
    CHECK (gestures.active() == 0);
    const auto selected = settings (processor, 0);
    type->showPopup();
    REQUIRE (type->isPopupActive());
    editor->setSize (1013, 900);
    CHECK_FALSE (type->isPopupActive());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (settings (processor, 0) == selected);
    CHECK (gestures.active() == 0);
    type->showPopup();
    popup = juce::Component::getCurrentlyModalComponent();
    REQUIRE (popup != nullptr);
    CHECK (popup->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (settings (processor, 0) == selected);
    type->showPopup();
    REQUIRE (type->isPopupActive());
    const auto otherBand = settings (processor, 1);
    card->setBand (1);
    CHECK_FALSE (type->isPopupActive());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (settings (processor, 0) == selected);
    CHECK (settings (processor, 1) == otherBand);
    type->showPopup();
    REQUIRE (type->isPopupActive());
    card->setBand (openq::bandCount);
    CHECK_FALSE (type->isPopupActive());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    card->setBand (0);
    type->showPopup();
    REQUIRE (type->isPopupActive());
    editor.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
    CHECK (juce::Desktop::getInstance().getNumComponents() == desktopComponents);
    openq::CompactComboBox orphan;
    orphan.addItem ("Bell", 1);
    orphan.showPopup();
    CHECK_FALSE (orphan.isPopupActive());
    CHECK (juce::Desktop::getInstance().getNumComponents() == desktopComponents);
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
    graph->mouseDown (mouse (*graph, { static_cast<float> (graph->getWidth()) * 0.25f, static_cast<float> (graph->getHeight()) * 0.5f }));
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
    set (processor, 0, "frequency", 100.0f);
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

TEST_CASE ("Cut card controls follow type slope automation and retain inactive settings", "[cuts][nodes][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditor());
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor->findChildWithID ("response.graph"));
    REQUIRE (graph != nullptr);
    auto& first = node (*graph, 0);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    auto* card = editor->findChildWithID ("band.card");
    auto* slope = dynamic_cast<juce::ComboBox*> (card->findChildWithID ("band.slope"));
    auto* type = dynamic_cast<juce::ComboBox*> (card->findChildWithID ("band.type"));
    auto* gain = dynamic_cast<juce::Slider*> (card->findChildWithID ("band.gain"));
    auto* q = dynamic_cast<juce::Slider*> (card->findChildWithID ("band.q"));
    REQUIRE (slope != nullptr);
    REQUIRE (type != nullptr);
    REQUIRE (gain != nullptr);
    REQUIRE (q != nullptr);
    CHECK_FALSE (slope->isEnabled());
    CHECK_FALSE (slope->getWantsKeyboardFocus());
    CHECK_FALSE (slope->getMouseClickGrabsKeyboardFocus());
    slope->showPopup();
    CHECK_FALSE (slope->isPopupActive());
    set (processor, 0, "gain", 12.0f);
    set (processor, 0, "q", 3.0f);
    type->setSelectedItemIndex (static_cast<int> (openq::FilterType::LowCut), juce::sendNotificationSync);
    CHECK (slope->isEnabled());
    CHECK (slope->getWantsKeyboardFocus());
    CHECK (slope->getMouseClickGrabsKeyboardFocus());
    CHECK_FALSE (gain->isEnabled());
    CHECK (q->isEnabled());
    CHECK_THAT (settings (processor, 0).gain, Catch::Matchers::WithinAbs (12.0, 0.001));
    CHECK_THAT (settings (processor, 0).q, Catch::Matchers::WithinAbs (3.0, 0.001));
    CHECK_THAT (q->getDoubleClickReturnValue(), Catch::Matchers::WithinAbs (openq::bandLimits::defaultCutQ, 0.0));
    slope->setSelectedItemIndex (static_cast<int> (openq::CutSlope::Db6), juce::sendNotificationSync);
    CHECK (settings (processor, 0).slope == openq::CutSlope::Db6);
    CHECK_FALSE (q->isEnabled());
    set (processor, 0, "slope", static_cast<float> (openq::CutSlope::Db96));
    CHECK (q->isEnabled());
    CHECK (slope->getSelectedItemIndex() == static_cast<int> (openq::CutSlope::Db96));
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    set (processor, 0, "type", static_cast<float> (openq::FilterType::Bell));
    CHECK (gain->isEnabled());
    CHECK_FALSE (slope->isEnabled());
    CHECK_FALSE (slope->getMouseClickGrabsKeyboardFocus());
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (type->getSelectedItemIndex() == static_cast<int> (openq::FilterType::LowCut));
    CHECK_FALSE (gain->isEnabled());
    CHECK (slope->isEnabled());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::FileOutputStream output (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-cut-card.png"));
    output.setPosition (0);
    output.truncate();
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, output));
}

TEST_CASE ("Disabled compact selectors never paint an active highlight", "[nodes][ui][card]")
{
    openq::CompactComboBox selector;
    selector.setSize (78, 26);
    const auto render = [&] (bool buttonDown) {
        juce::Image image (juce::Image::ARGB, selector.getWidth(), selector.getHeight(), true);
        juce::Graphics graphics (image);
        selector.getLookAndFeel().drawComboBox (graphics, image.getWidth(), image.getHeight(), buttonDown, 0, 0, 0, 0, selector);
        return image;
    };
    const auto different = [] (const juce::Image& a, const juce::Image& b) {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return true;
        return false;
    };
    CHECK (different (render (false), render (true)));
    selector.setEnabled (false);
    CHECK_FALSE (different (render (false), render (true)));
    CHECK_FALSE (selector.getWantsKeyboardFocus());
    CHECK_FALSE (selector.getMouseClickGrabsKeyboardFocus());
    selector.setEnabled (true);
    CHECK (different (render (false), render (true)));
    CHECK (selector.getWantsKeyboardFocus());
    CHECK (selector.getMouseClickGrabsKeyboardFocus());
    juce::Component parent;
    parent.addAndMakeVisible (selector);
    parent.setEnabled (false);
    CHECK_FALSE (selector.isEnabled());
    CHECK_FALSE (different (render (false), render (true)));
    CHECK_FALSE (selector.getMouseClickGrabsKeyboardFocus());
    parent.setEnabled (true);
    CHECK (selector.getMouseClickGrabsKeyboardFocus());
}

TEST_CASE ("Cut nodes stay at zero dB ignore vertical edits and disable first-order Q wheel", "[cuts][nodes][ui]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    Gestures gestures (processor);
    openq::ResponseGraph graph (processor.parameters, processor.getResponseSampleRate());
    graph.setSize (720, 280);
    const auto zeroY = node (graph, 0).getBounds().getCentreY();
    for (auto type : { openq::FilterType::LowCut, openq::FilterType::HighCut })
    {
        set (processor, 0, "type", static_cast<float> (type));
        set (processor, 0, "gain", 18.0f);
        set (processor, 0, "frequency", 1000.0f);
        graph.setSelectedBand (0);
        auto& first = node (graph, 0);
        CHECK (first.getBounds().getCentreY() == zeroY);
        first.mouseDown (mouse (first, { 12.0f, 12.0f }));
        CHECK (gestures.active() == 1);
        first.mouseDrag (mouse (first, { 100.0f, -10000.0f }));
        CHECK (settings (processor, 0).frequency > 1000.0);
        CHECK_THAT (settings (processor, 0).gain, Catch::Matchers::WithinAbs (18.0, 0.001));
        CHECK (first.getBounds().getCentreY() == zeroY);
        first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
        CHECK (gestures.active() == 0);
        set (processor, 0, "slope", static_cast<float> (openq::CutSlope::Db6));
        const auto qBefore = settings (processor, 0).q;
        juce::MouseWheelDetails wheel;
        wheel.deltaY = 0.25f;
        first.mouseWheelMove (mouse (first, { 12.0f, 12.0f }, 0), wheel);
        CHECK_THAT (settings (processor, 0).q, Catch::Matchers::WithinAbs (qBefore, 0.0));
        set (processor, 0, "slope", static_cast<float> (openq::CutSlope::Db18));
        first.mouseWheelMove (mouse (first, { 12.0f, 12.0f }, 0), wheel);
        CHECK (settings (processor, 0).q > qBefore);
    }
}

TEST_CASE ("Editor continuously scales cards and nodes with width while height adds graph space", "[resize][ui][card]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    PluginEditor editor (processor);
    REQUIRE (editor.isResizable());
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor.findChildWithID ("response.graph"));
    auto* card = editor.findChildWithID ("band.card");
    REQUIRE (graph != nullptr);
    REQUIRE (card != nullptr);
    auto& first = node (*graph, 0);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    REQUIRE (card->isVisible());
    const auto parameters = processor.parameters.copyState();
    auto* frequency = dynamic_cast<juce::Slider*> (card->findChildWithID ("band.frequency"));
    auto* selector = dynamic_cast<juce::ComboBox*> (card->findChildWithID ("band.type"));
    REQUIRE (frequency != nullptr);
    REQUIRE (selector != nullptr);
    const auto originalCard = card->getBounds();
    const auto originalKnob = frequency->getBounds();
    const auto originalNode = first.getBounds();
    const auto originalGraphHeight = graph->getHeight();
    const auto originalFont = selector->getLookAndFeel().getComboBoxFont (*selector).getHeight();
    editor.setSize (openq::EditorSize::defaultWidth, 800);
    CHECK (card->getWidth() == originalCard.getWidth());
    CHECK (card->getHeight() == originalCard.getHeight());
    CHECK (frequency->getBounds() == originalKnob);
    CHECK (graph->getHeight() > originalGraphHeight);

    struct Layout
    {
        int width;
        float scale;
    };
    for (const auto layout : { Layout { 1013, 1.186147f }, Layout { 1440, 1.414214f }, Layout { 2160, 1.732051f } })
    {
        const auto [width, scale] = layout;
        editor.setSize (width, juce::roundToInt (openq::EditorSize::defaultHeight * scale));
        CHECK_THAT (card->getWidth(), Catch::Matchers::WithinAbs (originalCard.getWidth() * scale, 1.0));
        CHECK_THAT (card->getHeight(), Catch::Matchers::WithinAbs (originalCard.getHeight() * scale, 1.0));
        CHECK_THAT (first.getWidth(), Catch::Matchers::WithinAbs (originalNode.getWidth() * openq::UiScale (width).graph(), 1.0));
        CHECK_THAT (selector->getLookAndFeel().getComboBoxFont (*selector).getHeight(), Catch::Matchers::WithinAbs (originalFont * scale, 0.5));
        CHECK (graph->getBounds().contains (card->getBounds()));
        CHECK_FALSE (card->getBounds().intersects (editor.findChildWithID ("editor.header")->getBounds()));
        CHECK_FALSE (card->getBounds().intersects (editor.findChildWithID ("editor.bottom")->getBounds()));
        CHECK_FALSE (card->getBounds().intersects (first.getBounds().translated (graph->getX(), graph->getY())));
        CHECK (editor.getTransform().isIdentity());
        CHECK (processor.getEditorSize() == openq::EditorSize { editor.getWidth(), editor.getHeight() });
    }
    editor.setSize (1440, 1160);
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    juce::FileOutputStream output (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-resized-card.png"));
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, output));
    // Short/wide host requests are constrained so the enlarged card still fits.
    editor.setSize (openq::EditorSize::maximumWidth, openq::EditorSize::minimumHeight);
    CHECK (editor.getHeight() == openq::EditorSize::minimumHeightForWidth (editor.getWidth()));
    CHECK (graph->getBounds().contains (card->getBounds()));
    CHECK (processor.parameters.copyState().isEquivalentTo (parameters));
    // If every corner intersects a centered node, shrinking must still retain the entire card.
    set (processor, 0, "frequency", 500.0f);
    graph->setSelectedBand (0);
    editor.setSize (openq::EditorSize::minimumWidth, openq::EditorSize::minimumHeight);
    CHECK (graph->getBounds().contains (card->getBounds()));
    editor.setSize (1, 1);
    CHECK (editor.getWidth() == openq::EditorSize::minimumWidth);
    CHECK (editor.getHeight() == openq::EditorSize::minimumHeight);
}

TEST_CASE ("Resize ends graph gestures and preserves an ongoing knob gesture until release", "[resize][ui][nodes]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    PluginEditor editor (processor);
    Gestures gestures (processor);
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor.findChildWithID ("response.graph"));
    REQUIRE (graph != nullptr);
    auto& first = node (*graph, 0);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    REQUIRE (gestures.active() == 2);
    editor.setSize (900, 750);
    CHECK (gestures.active() == 0);
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    auto* card = editor.findChildWithID ("band.card");
    REQUIRE (card != nullptr);
    auto* gain = dynamic_cast<juce::Slider*> (card->findChildWithID ("band.gain"));
    REQUIRE (gain != nullptr);
    {
        juce::Slider::ScopedDragNotification drag (*gain);
        REQUIRE (gestures.active() == 1);
        editor.setSize (1100, 900);
        CHECK (gestures.active() == 1);
        CHECK_FALSE (graph->isEnabled());
        CHECK (graph->getBounds().contains (card->getBounds()));
        CHECK_FALSE (card->getBounds().intersects (editor.findChildWithID ("editor.header")->getBounds()));
        CHECK_FALSE (card->getBounds().intersects (editor.findChildWithID ("editor.bottom")->getBounds()));
    }
    CHECK (gestures.active() == 0);
    CHECK (graph->isEnabled());
}

TEST_CASE ("Widescreen resizing reserves space for the graph instead of oversized controls", "[resize][ui][card]")
{
    PluginProcessor processor;
    processor.prepareToPlay (44100.0, 64);
    PluginEditor editor (processor);
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor.findChildWithID ("response.graph"));
    auto* card = editor.findChildWithID ("band.card");
    REQUIRE (graph != nullptr);
    REQUIRE (card != nullptr);
    auto& first = node (*graph, 0);
    first.mouseDown (mouse (first, { 12.0f, 12.0f }));
    first.mouseUp (mouse (first, { 12.0f, 12.0f }, 0));
    const auto initialCardWidth = card->getWidth();
    editor.setSize (1880, 1000);
    CHECK (editor.getHeight() == 1000);
    auto* bottom = editor.findChildWithID ("editor.bottom");
    REQUIRE (bottom != nullptr);
    CHECK (graph->getHeight() > (editor.getHeight() - bottom->getHeight()) * 0.88);
    CHECK (card->getWidth() > initialCardWidth);
    CHECK (card->getWidth() < editor.getWidth() * 0.34);
    CHECK (card->getHeight() < graph->getHeight() * 0.32);
    CHECK (graph->getBounds().contains (card->getBounds()));
    auto* selector = dynamic_cast<juce::ComboBox*> (card->findChildWithID ("band.type"));
    REQUIRE (selector != nullptr);
    CHECK (selector->getLookAndFeel().getComboBoxFont (*selector).getHeight() < 21.0f);
    const auto cardSize = card->getLocalBounds();
    editor.setSize (1880, 1200);
    CHECK (card->getLocalBounds() == cardSize);
    editor.setSize (1880, 1000);
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    juce::FileOutputStream output (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-widescreen-card.png"));
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, output));
}

TEST_CASE ("Closing focused overlays does not restore node focus or reopen a band card", "[nodes][ui][focus][card][analyzer]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Focus restoration requires an editor peer.");
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    set (processor, 1, "status", static_cast<float> (openq::BandStatus::On));
    PluginEditor editor (processor);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor.findChildWithID ("response.graph"));
    auto* card = editor.findChildWithID ("band.card");
    REQUIRE (graph != nullptr);
    REQUIRE (card != nullptr);
    auto* close = dynamic_cast<juce::Button*> (card->findChildWithID ("band.close"));
    REQUIRE (close != nullptr);
    const auto parameters = processor.parameters.copyState();
    for (const size_t band : { size_t { 0 }, size_t { 1 } })
    {
        node (*graph, band).grabKeyboardFocus();
        REQUIRE (graph->getSelectedBand() == band);
        REQUIRE (card->isVisible());
        close->grabKeyboardFocus();
        REQUIRE (close->hasKeyboardFocus (false));
        close->onClick();
        CHECK_FALSE (card->isVisible());
        CHECK (graph->getSelectedBand() == openq::bandCount);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK_FALSE (card->isVisible());
        CHECK (graph->getSelectedBand() == openq::bandCount);
    }
    auto* analyzerButton = dynamic_cast<juce::Button*> (editor.findChildWithID ("editor.bottom")->findChildWithID ("analyzer.settings"));
    REQUIRE (analyzerButton != nullptr);
    const auto openAnalyzer = [&] {
        analyzerButton->setToggleState (true, juce::dontSendNotification);
        analyzerButton->onClick();
    };
    openAnalyzer();
    auto* analyzer = editor.findChildWithID ("analyzer.panel");
    REQUIRE (analyzer != nullptr);
    auto* analyzerClose = dynamic_cast<juce::Button*> (analyzer->findChildWithID ("analyzer.close"));
    REQUIRE (analyzerClose != nullptr);
    analyzerClose->grabKeyboardFocus();
    REQUIRE (analyzerClose->hasKeyboardFocus (false));
    analyzerClose->onClick();
    CHECK_FALSE (analyzer->isVisible());
    CHECK_FALSE (card->isVisible());
    CHECK (graph->getSelectedBand() == openq::bandCount);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_FALSE (card->isVisible());
    for (const bool escape : { false, true })
    {
        openAnalyzer();
        auto* quality = analyzer->findChildWithID ("analyzer.quality");
        REQUIRE (quality != nullptr);
        quality->grabKeyboardFocus();
        REQUIRE (analyzer->hasKeyboardFocus (true));
        if (escape)
            CHECK (analyzer->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        else
        {
            analyzerButton->setToggleState (false, juce::dontSendNotification);
            analyzerButton->onClick();
        }
        CHECK_FALSE (analyzer->isVisible());
        CHECK_FALSE (card->isVisible());
        CHECK (graph->getSelectedBand() == openq::bandCount);
    }
    // Closing settings also preserves an existing selection instead of jumping to another band.
    node (*graph, 1).grabKeyboardFocus();
    REQUIRE (graph->getSelectedBand() == 1);
    openAnalyzer();
    analyzerClose->grabKeyboardFocus();
    analyzerClose->onClick();
    CHECK (graph->getSelectedBand() == 1);
    CHECK (card->isVisible());
    CHECK (processor.parameters.copyState().isEquivalentTo (parameters));
}
