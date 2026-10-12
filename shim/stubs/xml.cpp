#include "xml.h"
#include "generated/xml.h"
#include <wrl.h>
#include <string>
#include <utility>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Data::Xml::Dom;
using ABI::Windows::UI::Notifications::ToastTemplateType;
namespace ctr2 {
namespace {
class Node;
// Private identity check: only nodes from this DOM can be linked into it.
struct __declspec(uuid("3c5e8f2a-9d41-4b7e-8f06-2a1d7c94e5b3")) INodeImpl : public IInspectable {
    virtual Node* STDMETHODCALLTYPE Impl() = 0;
};
std::wstring Text(HSTRING value) {
    UINT32 length{};
    const auto* raw = WindowsGetStringRawBuffer(value, &length);
    return std::wstring(raw, length);
}
HRESULT Copy(const std::wstring& text, HSTRING* value) {
    if (!value) return E_POINTER;
    return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
}
std::wstring Escape(const std::wstring& text, bool attribute) {
    std::wstring out;
    for (auto c : text) {
        if (c == L'&')
            out += L"&amp;";
        else if (c == L'<')
            out += L"&lt;";
        else if (c == L'>')
            out += L"&gt;";
        else if (attribute && c == L'"')
            out += L"&quot;";
        else
            out += c;
    }
    return out;
}
using NodeView = ABI::Windows::Foundation::Collections::IVectorView<IXmlNode*>;
using NodeIterable = ABI::Windows::Foundation::Collections::IIterable<IXmlNode*>;
using NodeIteratorType = ABI::Windows::Foundation::Collections::IIterator<IXmlNode*>;
class NodeIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, NodeIteratorType, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IIterator`1<Windows.Data.Xml.Dom.IXmlNode>", BaseTrust);
    std::vector<ComPtr<IXmlNode>> items_;
    UINT32 index_ = 0;

public:
    explicit NodeIterator(std::vector<ComPtr<IXmlNode>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return index_ < items_.size() ? items_[index_].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = index_ < items_.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        if (index_ < items_.size()) ++index_;
        return get_HasCurrent(value);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, IXmlNode** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        while (*actual < capacity && index_ < items_.size()) items_[index_++].CopyTo(&values[(*actual)++]);
        return S_OK;
    }
};
// XmlNodeList also projects as IVectorView/IIterable<IXmlNode>; C++/CX uses
// those for GetAt and range-for.
class NodeList final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIXmlNodeList, NodeView, NodeIterable, FtmBase> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlNodeList", BaseTrust);
    CTR2_LOG_MISSING_INTERFACES("NodeList")
    std::vector<ComPtr<IXmlNode>> items_;

public:
    explicit NodeList(std::vector<ComPtr<IXmlNode>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(items_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Item(UINT32 index, IXmlNode** node) override {
        if (!node) return E_POINTER;
        *node = nullptr;
        return index < items_.size() ? items_[index].CopyTo(node) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, IXmlNode** node) override {
        if (!node) return E_POINTER;
        *node = nullptr;
        return index < items_.size() ? items_[index].CopyTo(node) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override { return get_Length(value); }
    HRESULT STDMETHODCALLTYPE IndexOf(IXmlNode* node, UINT32* index, boolean* found) override {
        if (!index || !found) return E_POINTER;
        *index = 0;
        *found = false;
        for (UINT32 i = 0; i < items_.size(); ++i)
            if (items_[i].Get() == node) {
                *index = i;
                *found = true;
                break;
            }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, IXmlNode** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        if (start > items_.size()) return E_BOUNDS;
        for (UINT32 i = start; i < items_.size() && *actual < capacity; ++i) items_[i].CopyTo(&values[(*actual)++]);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE First(NodeIteratorType** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto iterator = Make<NodeIterator>(items_);
        return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
    }
};
// Shared tree state. Children are owned; the parent link is borrowed and
// cleared when the parent goes away.
class Node {
public:
    NodeType type_;
    std::wstring name_, data_;
    std::vector<std::pair<std::wstring, std::wstring>> attributes_;
    std::vector<ComPtr<IXmlNode>> children_;
    Node* parent_ = nullptr;
    IXmlNode* self_ = nullptr;
    Node(NodeType type, std::wstring name) : type_(type), name_(std::move(name)) {}
    virtual ~Node() {
        for (auto& child : children_)
            if (auto* node = From(child.Get())) node->parent_ = nullptr;
    }
    static Node* From(IUnknown* value) {
        ComPtr<INodeImpl> impl;
        return value && SUCCEEDED(value->QueryInterface(IID_PPV_ARGS(&impl))) ? impl->Impl() : nullptr;
    }
    void Detach() {
        if (!parent_) return;
        auto& siblings = parent_->children_;
        for (auto it = siblings.begin(); it != siblings.end(); ++it)
            if (it->Get() == self_) {
                parent_ = nullptr;
                siblings.erase(it);
                return;
            }
        parent_ = nullptr;
    }
    void Collect(const std::wstring& tag, std::vector<ComPtr<IXmlNode>>& found) const {
        for (const auto& child : children_) {
            auto* node = From(child.Get());
            if (node && node->type_ == NodeType_ElementNode) {
                if (tag == L"*" || node->name_ == tag) found.push_back(child);
                node->Collect(tag, found);
            }
        }
    }
    std::wstring InnerText() const {
        if (type_ == NodeType_TextNode) return data_;
        std::wstring text;
        for (const auto& child : children_)
            if (auto* node = From(child.Get())) text += node->InnerText();
        return text;
    }
    std::wstring Xml() const {
        if (type_ == NodeType_TextNode) return Escape(data_, false);
        std::wstring inner;
        for (const auto& child : children_)
            if (auto* node = From(child.Get())) inner += node->Xml();
        if (type_ == NodeType_DocumentNode) return inner;
        std::wstring xml = L"<" + name_;
        for (const auto& attribute : attributes_)
            xml += L" " + attribute.first + L"=\"" + Escape(attribute.second, true) + L"\"";
        return inner.empty() ? xml + L"/>" : xml + L">" + inner + L"</" + name_ + L">";
    }
};
template <class Derived, class... Interfaces>
class NodeBase : public RuntimeClass<RuntimeClassFlags<WinRt>, Interfaces..., DefaultIXmlNode, DefaultIXmlNodeSelector,
                                     DefaultIXmlNodeSerializer, INodeImpl, FtmBase>,
                 public Node {
    friend Derived;
    NodeBase(NodeType type, std::wstring name) : Node(type, std::move(name)) { self_ = static_cast<IXmlNode*>(this); }

public:
    Node* STDMETHODCALLTYPE Impl() override { return this; }
    HRESULT STDMETHODCALLTYPE get_NodeName(HSTRING* value) override {
        return Copy(type_ == NodeType_TextNode       ? L"#text"
                    : type_ == NodeType_DocumentNode ? L"#document"
                                                     : name_,
                    value);
    }
    HRESULT STDMETHODCALLTYPE get_NodeType(NodeType* value) override {
        if (!value) return E_POINTER;
        *value = type_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ParentNode(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = parent_ ? parent_->self_ : nullptr;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ChildNodes(IXmlNodeList** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto list = Make<NodeList>(children_);
        return list ? list.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_FirstChild(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return children_.empty() ? S_OK : children_.front().CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_LastChild(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return children_.empty() ? S_OK : children_.back().CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE HasChildNodes(boolean* value) override {
        if (!value) return E_POINTER;
        *value = !children_.empty();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AppendChild(IXmlNode* child, IXmlNode** appended) override {
        if (!appended) return E_POINTER;
        *appended = nullptr;
        auto* node = From(child);
        if (!node || node->type_ == NodeType_DocumentNode || type_ == NodeType_TextNode) return E_INVALIDARG;
        for (auto* ancestor = static_cast<Node*>(this); ancestor; ancestor = ancestor->parent_)
            if (ancestor == node) return E_INVALIDARG;
        ComPtr<IXmlNode> retained(child);
        node->Detach();
        children_.push_back(retained);
        node->parent_ = this;
        return retained.CopyTo(appended);
    }
    HRESULT STDMETHODCALLTYPE RemoveChild(IXmlNode* child, IXmlNode** removed) override {
        if (!removed) return E_POINTER;
        *removed = nullptr;
        auto* node = From(child);
        if (!node || node->parent_ != this) return E_INVALIDARG;
        ComPtr<IXmlNode> retained(child);
        node->Detach();
        return retained.CopyTo(removed);
    }
    HRESULT STDMETHODCALLTYPE get_InnerText(HSTRING* value) override { return Copy(InnerText(), value); }
    HRESULT STDMETHODCALLTYPE GetXml(HSTRING* value) override { return Copy(Xml(), value); }
};
class TextNode final : public NodeBase<TextNode, DefaultIXmlText, DefaultIXmlCharacterData> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlText", BaseTrust);
    CTR2_LOG_MISSING_INTERFACES("TextNode")
public:
    explicit TextNode(std::wstring data) : NodeBase(NodeType_TextNode, L"") { data_ = std::move(data); }
    HRESULT STDMETHODCALLTYPE get_Data(HSTRING* value) override { return Copy(data_, value); }
    HRESULT STDMETHODCALLTYPE put_Data(HSTRING value) override {
        data_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(data_.size());
        return S_OK;
    }
};
class Element final : public NodeBase<Element, DefaultIXmlElement> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlElement", BaseTrust);
    CTR2_LOG_MISSING_INTERFACES("Element")
public:
    explicit Element(std::wstring name) : NodeBase(NodeType_ElementNode, std::move(name)) {}
    HRESULT STDMETHODCALLTYPE get_TagName(HSTRING* value) override { return Copy(name_, value); }
    HRESULT STDMETHODCALLTYPE GetAttribute(HSTRING name, HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto key = Text(name);
        for (auto& attribute : attributes_)
            if (attribute.first == key) return Copy(attribute.second, value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAttribute(HSTRING name, HSTRING value) override {
        auto key = Text(name);
        if (key.empty()) return E_INVALIDARG;
        for (auto& attribute : attributes_)
            if (attribute.first == key) {
                attribute.second = Text(value);
                return S_OK;
            }
        attributes_.emplace_back(key, Text(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetElementsByTagName(HSTRING tag, IXmlNodeList** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::vector<ComPtr<IXmlNode>> found;
        Collect(Text(tag), found);
        auto list = Make<NodeList>(std::move(found));
        return list ? list.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class Document final : public NodeBase<Document, DefaultIXmlDocument> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlDocument", BaseTrust);
    CTR2_LOG_MISSING_INTERFACES("Document")
public:
    Document() : NodeBase(NodeType_DocumentNode, L"") {}
    HRESULT STDMETHODCALLTYPE CreateElement(HSTRING tag, IXmlElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto name = Text(tag);
        if (name.empty()) return E_INVALIDARG;
        auto element = Make<Element>(name);
        return element ? element.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateTextNode(HSTRING data, IXmlText** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto text = Make<TextNode>(Text(data));
        return text ? text.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_DocumentElement(IXmlElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        for (auto& child : children_)
            if (SUCCEEDED(child.CopyTo(value))) return S_OK;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetElementsByTagName(HSTRING tag, IXmlNodeList** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::vector<ComPtr<IXmlNode>> found;
        Collect(Text(tag), found);
        auto list = Make<NodeList>(std::move(found));
        return list ? list.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT Append(IXmlNode* parent, const wchar_t* name,
               std::initializer_list<std::pair<const wchar_t*, const wchar_t*>> attributes, ComPtr<IXmlNode>& child) {
    auto element = Make<Element>(name);
    if (!element) return E_OUTOFMEMORY;
    for (const auto& attribute : attributes) element->attributes_.emplace_back(attribute.first, attribute.second);
    ComPtr<IXmlNode> node;
    auto hr = element.As(&node);
    if (FAILED(hr)) return hr;
    ComPtr<IXmlNode> appended;
    hr = parent->AppendChild(node.Get(), &appended);
    if (FAILED(hr)) return hr;
    child = std::move(node);
    return S_OK;
}
}
HRESULT MakeToastTemplate(ToastTemplateType type, IXmlDocument** value) {
    using namespace ABI::Windows::UI::Notifications;
    if (!value) return E_POINTER;
    *value = nullptr;
    // Legacy Windows 8.1 toast template shapes: optional image plus 1-3 text lines.
    const wchar_t* name;
    bool image = false;
    int texts;
    switch (type) {
    case ToastTemplateType_ToastImageAndText01:
        name = L"ToastImageAndText01";
        image = true;
        texts = 1;
        break;
    case ToastTemplateType_ToastImageAndText02:
        name = L"ToastImageAndText02";
        image = true;
        texts = 2;
        break;
    case ToastTemplateType_ToastImageAndText03:
        name = L"ToastImageAndText03";
        image = true;
        texts = 2;
        break;
    case ToastTemplateType_ToastImageAndText04:
        name = L"ToastImageAndText04";
        image = true;
        texts = 3;
        break;
    case ToastTemplateType_ToastText01:
        name = L"ToastText01";
        texts = 1;
        break;
    case ToastTemplateType_ToastText02:
        name = L"ToastText02";
        texts = 2;
        break;
    case ToastTemplateType_ToastText03:
        name = L"ToastText03";
        texts = 2;
        break;
    case ToastTemplateType_ToastText04:
        name = L"ToastText04";
        texts = 3;
        break;
    default:
        return E_INVALIDARG;
    }
    auto document = Make<Document>();
    if (!document) return E_OUTOFMEMORY;
    ComPtr<IXmlNode> root, toast, visual, binding, child;
    auto hr = document.As(&root);
    if (FAILED(hr)) return hr;
    hr = Append(root.Get(), L"toast", {}, toast);
    if (FAILED(hr)) return hr;
    hr = Append(toast.Get(), L"visual", {}, visual);
    if (FAILED(hr)) return hr;
    hr = Append(visual.Get(), L"binding", {{L"template", name}}, binding);
    if (FAILED(hr)) return hr;
    if (image) {
        hr = Append(binding.Get(), L"image", {{L"id", L"1"}, {L"src", L""}}, child);
        if (FAILED(hr)) return hr;
    }
    static const wchar_t* ids[] = {L"1", L"2", L"3"};
    for (int i = 0; i < texts; ++i) {
        hr = Append(binding.Get(), L"text", {{L"id", ids[i]}}, child);
        if (FAILED(hr)) return hr;
    }
    return document.CopyTo(value);
}
}
