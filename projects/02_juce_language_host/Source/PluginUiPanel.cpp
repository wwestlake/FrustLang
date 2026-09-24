#include "PluginUiPanel.h"

namespace {
juce::String stringProp(const juce::var& object, const juce::Identifier& name, const juce::String& fallback = {})
{
    if (!object.isObject()) return fallback;
    const auto value = object.getProperty(name, {});
    return value.isVoid() ? fallback : value.toString();
}

void addLabel(std::vector<std::unique_ptr<juce::Component>>& controls,
              juce::Component& owner,
              const juce::String& text,
              bool bold = false)
{
    auto label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);
    label->setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    if (bold) label->setFont(juce::Font(14.0f, juce::Font::bold));
    label->setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(*label);
    controls.push_back(std::move(label));
}
}

PluginUiPanel::PluginUiPanel(juce::String panelIdIn,
                             juce::String titleIn,
                             juce::var panelManifestIn,
                             std::shared_ptr<PluginRuntimeState> runtimeIn,
                             std::function<void(const juce::String&)> logFnIn)
    : panelId(std::move(panelIdIn)),
      title(std::move(titleIn)),
      panelManifest(std::move(panelManifestIn)),
      runtime(std::move(runtimeIn)),
      logFn(std::move(logFnIn))
{
    titleLabel.setText(title, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(15.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7fffd4));
    addAndMakeVisible(titleLabel);

    statusView.setMultiLine(true);
    statusView.setReadOnly(true);
    statusView.setScrollbarsShown(true);
    statusView.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff101820));
    statusView.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
    statusView.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff53656d));
    addAndMakeVisible(statusView);

    buildControls();
    setStatus("Plugin UI ready.");
}

void PluginUiPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void PluginUiPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    titleLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(6);

    for (auto& control : controls)
    {
        const auto* editor = dynamic_cast<juce::TextEditor*>(control.get());
        const auto* button = dynamic_cast<juce::TextButton*>(control.get());
        const int height = editor != nullptr && editor->isMultiLine() ? 86
                         : button != nullptr ? 28
                         : 24;
        control->setBounds(bounds.removeFromTop(height));
        bounds.removeFromTop(5);
    }

    bounds.removeFromTop(4);
    statusView.setBounds(bounds);
}

void PluginUiPanel::buildControls()
{
    controls.clear();
    valueEditors.clear();

    if (!panelManifest.isObject())
    {
        setStatus("Invalid plugin UI manifest panel.");
        return;
    }

    auto controlList = panelManifest.getProperty("controls", {});
    auto* controlArray = controlList.getArray();
    if (controlArray == nullptr)
    {
        addLabel(controls, *this, "This plugin UI has no controls.");
        return;
    }

    for (const auto& controlDef : *controlArray)
    {
        const auto type = stringProp(controlDef, "type").trim().toLowerCase();
        const auto id = stringProp(controlDef, "id").trim();
        const auto label = stringProp(controlDef, "label", stringProp(controlDef, "text"));

        if (type == "label")
        {
            addLabel(controls, *this, stringProp(controlDef, "text", label));
        }
        else if (type == "text" || type == "textarea")
        {
            addLabel(controls, *this, label.isNotEmpty() ? label : id, true);
            auto editor = std::make_unique<juce::TextEditor>();
            editor->setMultiLine(type == "textarea");
            editor->setReturnKeyStartsNewLine(type == "textarea");
            editor->setScrollbarsShown(true);
            editor->setTextToShowWhenEmpty(stringProp(controlDef, "placeholder"), juce::Colours::grey);
            editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff24333a));
            editor->setColour(juce::TextEditor::textColourId, juce::Colour(0xfff0f6f8));
            editor->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff6b7e86));
            if (id.isNotEmpty()) valueEditors[id] = editor.get();
            addAndMakeVisible(*editor);
            controls.push_back(std::move(editor));
        }
        else if (type == "button")
        {
            auto button = std::make_unique<juce::TextButton>(label.isNotEmpty() ? label : id);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4b63));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0f6f8));
            button->onClick = [this, id] { handleButtonClick(id); };
            addAndMakeVisible(*button);
            controls.push_back(std::move(button));
        }
        else
        {
            addLabel(controls, *this, "Unsupported plugin UI control: " + type);
        }
    }
}

juce::var PluginUiPanel::collectValues() const
{
    auto values = std::make_unique<juce::DynamicObject>();
    for (const auto& [id, editor] : valueEditors)
        if (editor != nullptr)
            values->setProperty(id, editor->getText());
    return juce::var(values.release());
}

void PluginUiPanel::handleButtonClick(const juce::String& controlId)
{
    if (runtime == nullptr || !runtime->alive || runtime->handle == nullptr)
    {
        setStatus("Plugin is unloaded; this panel can no longer send events.");
        return;
    }

    void* rawFn = frust_plugin_get_fn(runtime->handle, "frusty_ui_handle_event_json");
    if (rawFn == nullptr)
    {
        setStatus("Plugin has no frusty_ui_handle_event_json(eventJson) handler.");
        return;
    }

    auto event = std::make_unique<juce::DynamicObject>();
    event->setProperty("panelId", panelId);
    event->setProperty("controlId", controlId);
    event->setProperty("type", "click");
    event->setProperty("values", collectValues());
    const auto eventJson = juce::JSON::toString(juce::var(event.release()), false);

    using EventFn = const char* (*)(const char*);
    auto fn = reinterpret_cast<EventFn>(rawFn);
    const auto eventUtf8 = eventJson.toUTF8();
    const char* result = fn(eventUtf8.getAddress());
    const juce::String response = result != nullptr && *result != 0 ? juce::String(result) : "(plugin returned no response)";
    setStatus(response);
    if (logFn) logFn(runtime->displayName + " UI event " + controlId + " => " + response);
}

void PluginUiPanel::setStatus(const juce::String& text)
{
    statusView.setText(text, juce::dontSendNotification);
}
