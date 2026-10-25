#ifndef IMTG_UI_TREE_HPP
#define IMTG_UI_TREE_HPP

#include "imtg/core/result.hpp"
#include "imtg/actions/registry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace imtg {

enum class NodeKind {
    Root,
    Text,
    Heading,
    Section,
    Separator,
    Row,
    Button,
};

struct ButtonDescriptor {
    std::string action_id;
    std::uint32_t payload_version{};
    std::string payload;
    bool validated = false;

    friend bool operator==(const ButtonDescriptor& left,
                           const ButtonDescriptor& right) noexcept
    {
        return left.action_id == right.action_id && left.payload_version == right.payload_version
            && left.payload == right.payload && left.validated == right.validated;
    }
};

struct UiNode {
    NodeKind kind;
    std::string id;
    std::string path;
    std::string text;
    std::optional<ButtonDescriptor> button;
    std::vector<std::size_t> children;

    friend bool operator==(const UiNode& left, const UiNode& right) noexcept
    {
        return left.kind == right.kind && left.id == right.id && left.path == right.path
            && left.text == right.text && left.button == right.button
            && left.children == right.children;
    }
};

/// \brief Immutable result of one deterministic UI render.
class UiTree {
public:
    UiTree() = default;
    explicit UiTree(std::vector<UiNode> nodes) : m_nodes(std::move(nodes)) {}

    [[nodiscard]] const std::vector<UiNode>& nodes() const noexcept
    {
        return m_nodes;
    }

    friend bool operator==(const UiTree& left, const UiTree& right) noexcept
    {
        return left.m_nodes == right.m_nodes;
    }

private:
    std::vector<UiNode> m_nodes;
};

/// \brief Builds one render without global state or shared mutable context.
class UiBuilder {
public:
    class Scope {
    public:
        Scope() noexcept = default;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& other) noexcept;
        Scope& operator=(Scope&& other) noexcept;
        ~Scope();

        [[nodiscard]] bool is_active() const noexcept
        {
            return m_builder != nullptr;
        }

    private:
        friend class UiBuilder;
        Scope(UiBuilder* builder, std::size_t node_index) noexcept
            : m_builder(builder), m_node_index(node_index)
        {
        }

        UiBuilder* m_builder = nullptr;
        std::size_t m_node_index = 0;
    };

    explicit UiBuilder(ActionRegistry* registry = nullptr) : m_registry(registry) {}
    UiBuilder(const UiBuilder&) = delete;
    UiBuilder& operator=(const UiBuilder&) = delete;

    /// \brief Opens the required root scope.
    [[nodiscard]] Result<Scope> begin(std::string_view id);

    /// \brief Opens a nested section scope that closes at RAII scope end.
    [[nodiscard]] Result<Scope> section(std::string_view id, std::string_view title = {});

    /// \brief Opens a keyboard row/grouping scope.
    [[nodiscard]] Result<Scope> row(std::string_view id);

    [[nodiscard]] Result<void> text(std::string_view id, std::string_view value);
    [[nodiscard]] Result<void> heading(std::string_view id, std::string_view value);
    [[nodiscard]] Result<void> separator(std::string_view id);
    [[nodiscard]] Result<void> button(std::string_view id,
                                      std::string_view label,
                                      std::string_view action_id,
                                      std::string_view payload = {});

    template <typename Payload>
    [[nodiscard]] Result<void> button(std::string_view id,
                                      std::string_view label,
                                      const ActionHandle<Payload>& action,
                                      const Payload& payload)
    {
        const auto validation = m_registry != nullptr ? m_registry->validate_handle(action)
                                                       : action.validate();
        if (!validation) {
            return validation.error();
        }
        const auto encoded = action.encode_payload(payload);
        if (!encoded) {
            return encoded.error();
        }
        return append_node(NodeKind::Button, id, label,
                           ButtonDescriptor{action.action_id().value,
                                            action.payload_version(),
                                            encoded.value(),
                                            true});
    }

    template <typename Payload>
    [[nodiscard]] Result<void> button(std::string_view id,
                                      std::string_view label,
                                      const Payload& payload,
                                      const ActionHandle<Payload>& action)
    {
        return button(id, label, action, payload);
    }

    /// \brief Finishes a render after all RAII scopes have closed.
    [[nodiscard]] Result<UiTree> finish();

private:
    struct Frame {
        std::size_t node_index;
        std::unordered_set<std::string> child_ids;
    };

    [[nodiscard]] Result<Scope> open_scope(NodeKind kind,
                                            std::string_view id,
                                            std::string_view text);
    [[nodiscard]] Result<void> append_node(NodeKind kind,
                                           std::string_view id,
                                           std::string_view text,
                                           std::optional<ButtonDescriptor> button);
    void close_scope(std::size_t node_index) noexcept;

    std::vector<UiNode> m_nodes;
    std::vector<Frame> m_stack;
    bool m_finished = false;
    bool m_scope_error = false;
    ActionRegistry* m_registry = nullptr;
};

} // namespace imtg

#endif // IMTG_UI_TREE_HPP
