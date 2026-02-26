#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

/**
 * Lossless ValueTree <-> JSON conversion for Git-friendly edit state persistence.
 * 
 * Produces deterministic, sorted JSON output so that Git diffs are clean and 
 * meaningful. Properties are sorted alphabetically, one per line.
 */
namespace ValueTreeJSON
{
    /** Convert a ValueTree to a JUCE var (JSON-compatible object).
     *  Properties are stored as top-level keys, children as a "_children" array.
     *  Binary (MemoryBlock) data is stored as base64 strings with a "_b64:" prefix.
     */
    inline juce::var toVar(const juce::ValueTree& tree)
    {
        auto* obj = new juce::DynamicObject();
        
        // Type is always first
        obj->setProperty("_type", tree.getType().toString());
        
        // Collect and sort property names for deterministic output
        juce::StringArray propNames;
        for (int i = 0; i < tree.getNumProperties(); i++)
            propNames.add(tree.getPropertyName(i).toString());
        propNames.sort(false);
        
        // Write properties in sorted order
        for (auto& name : propNames)
        {
            auto val = tree.getProperty(name);
            
            // Handle MemoryBlock (binary data) as base64
            if (auto* mb = val.getBinaryData())
            {
                obj->setProperty(name, "_b64:" + mb->toBase64Encoding());
            }
            else
            {
                obj->setProperty(name, val);
            }
        }
        
        // Children
        if (tree.getNumChildren() > 0)
        {
            juce::Array<juce::var> children;
            for (int i = 0; i < tree.getNumChildren(); i++)
                children.add(toVar(tree.getChild(i)));
            obj->setProperty("_children", children);
        }
        
        return juce::var(obj);
    }
    
    /** Convert a JUCE var (JSON object) back to a ValueTree.
     *  Reverses the toVar() encoding.
     */
    inline juce::ValueTree fromVar(const juce::var& v)
    {
        auto* obj = v.getDynamicObject();
        if (!obj) return {};
        
        auto typeName = obj->getProperty("_type").toString();
        if (typeName.isEmpty()) return {};
        
        juce::ValueTree tree(typeName);
        
        for (auto& prop : obj->getProperties())
        {
            auto name = prop.name.toString();
            
            // Skip meta-keys
            if (name == "_type" || name == "_children")
                continue;
            
            // Decode base64 binary data
            if (prop.value.isString() && prop.value.toString().startsWith("_b64:"))
            {
                juce::MemoryBlock mb;
                mb.fromBase64Encoding(prop.value.toString().substring(5));
                tree.setProperty(prop.name, mb, nullptr);
            }
            else
            {
                tree.setProperty(prop.name, prop.value, nullptr);
            }
        }
        
        // Restore children
        auto childrenVar = obj->getProperty("_children");
        if (auto* childArray = childrenVar.getArray())
        {
            for (auto& childVar : *childArray)
                tree.addChild(fromVar(childVar), -1, nullptr);
        }
        
        return tree;
    }
    
    /** Serialize a ValueTree to a pretty-printed JSON string. */
    inline juce::String toJsonString(const juce::ValueTree& tree)
    {
        return juce::JSON::toString(toVar(tree), false);
    }
    
    /** Deserialize a JSON string back to a ValueTree. */
    inline juce::ValueTree fromJsonString(const juce::String& jsonText)
    {
        auto parsed = juce::JSON::parse(jsonText);
        if (parsed.isVoid()) return {};
        return fromVar(parsed);
    }
}
