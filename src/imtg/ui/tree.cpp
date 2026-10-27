#include "imtg/ui/tree.hpp"

namespace imtg {

namespace {

Error invalid_state(const char* message)
{
    return Error{ErrorCode::InvalidState, message};
}

} // namespace

UiBuilder::Scope::Scope(Scope&& other) noexcept
    : m_builder(other.m_builder), m_node_index(other.m_node_index)
{
    other.m_builder = nullptr;
}

UiBuilder::Scope& UiBuilder::Scope::operator=(Scope&& other) noexcept
{
    if (this != &other) {
        if (m_builder != nullptr) {
            m_builder->close_scope(m_node_index);
        }
        m_builder = other.m_builder;
        m_node_index = other.m_node_index;
        other.m_builder = nullptr;
    }
    return *this;
}

UiBuilder::Scope::~Scope()
{
    if (m_builder != nullptr) {
        m_builder->close_scope(m_node_index);
    }
}

Result<UiBuilder::Scope> UiBuilder::begin(std::string_view id)
{
    if (!m_nodes.empty() || !m_stack.empty() || m_finished) {
        return invalid_state("render root has already been opened or finished");
    }
    return open_scope(NodeKind::Root, id, {});
}

Result<UiBuilder::Scope> UiBuilder::section(std::string_view id, std::string_view title)
{
    return open_scope(NodeKind::Section, id, title);
}

Result<UiBuilder::Scope> UiBuilder::row(std::string_view id)
{
    return open_scope(NodeKind::Row, id, {});
}

Result<void> UiBuilder::text(std::string_view id, std::string_view value)
{
    return append_node(NodeKind::Text, id, value, std::nullopt);
}

Result<void> UiBuilder::heading(std::string_view id, std::string_view value)
{
    return append_node(NodeKind::Heading, id, value, std::nullopt);
}

Result<void> UiBuilder::separator(std::string_view id)
{
    return append_node(NodeKind::Separator, id, {}, std::nullopt);
}

Result<void> UiBuilder::button(std::string_view id,
                               std::string_view label,
                               std::string_view action_id,
                               std::string_view payload)
{
    if (action_id.empty()) {
        return Error{ErrorCode::InvalidArgument, "button action id cannot be empty"};
    }
    return append_node(NodeKind::Button, id, label,
                       ButtonDescriptor{std::string(action_id), 0u, std::string(payload), false});
}

Result<UiTree> UiBuilder::finish()
{
    if (m_finished || m_nodes.empty() || m_scope_error) {
        return invalid_state("render has no open root");
    }
    if (!m_stack.empty()) {
        return invalid_state("all render scopes must close before finish");
    }
    m_finished = true;
    return UiTree(std::move(m_nodes));
}

Result<UiBuilder::Scope> UiBuilder::open_scope(NodeKind kind,
                                                std::string_view id,
                                                std::string_view text)
{
    if (m_finished || m_scope_error || (kind != NodeKind::Root && m_stack.empty())) {
        return invalid_state("scope is outside an active render");
    }
    const auto result = append_node(kind, id, text, std::nullopt);
    if (!result) {
        return result.error();
    }

    const std::size_t node_index = m_nodes.size() - 1;
    m_stack.push_back(Frame{node_index, {}});
    return Scope(this, node_index);
}

Result<void> UiBuilder::append_node(NodeKind kind,
                                    std::string_view id,
                                    std::string_view text,
                                    std::optional<ButtonDescriptor> button)
{
    if (m_finished || m_scope_error || (m_stack.empty() && kind != NodeKind::Root)) {
        return invalid_state("node is outside an active render scope");
    }
    if (id.empty()) {
        return Error{ErrorCode::InvalidArgument, "component id cannot be empty"};
    }
    if (id.find('/') != std::string_view::npos) {
        return Error{ErrorCode::InvalidArgument, "component id cannot contain '/'"};
    }

    if (kind == NodeKind::Root) {
        if (!m_nodes.empty()) {
            return invalid_state("render root already exists");
        }
        m_nodes.push_back(UiNode{kind, std::string(id), std::string(id),
                                 std::string(text), std::move(button), {}});
        return Result<void>();
    }

    Frame& parent = m_stack.back();
    const auto inserted = parent.child_ids.emplace(id);
    if (!inserted.second) {
        return Error{ErrorCode::DuplicateId, "component id is duplicated in its scope"};
    }

    const UiNode& parent_node = m_nodes[parent.node_index];
    std::string path = parent_node.path;
    path += "/";
    path += id;
    const std::size_t node_index = m_nodes.size();
    m_nodes.push_back(UiNode{kind, std::string(id), std::move(path),
                             std::string(text), std::move(button), {}});
    m_nodes[parent.node_index].children.push_back(node_index);
    return Result<void>();
}

void UiBuilder::close_scope(std::size_t node_index) noexcept
{
    if (m_stack.empty() || m_stack.back().node_index != node_index) {
        m_scope_error = true;
        return;
    }
    m_stack.pop_back();
}

} // namespace imtg
