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

/// \brief Encoded action binding attached to a button node.
struct ButtonDescriptor {
    std::string action_id; ///< Canonical action route.
    std::uint32_t payload_version{}; ///< Typed payload schema version.
    std::string payload; ///< Encoded action payload.
    bool validated = false; ///< Whether the binding came from a live typed handle.

    /// \brief Compares all persisted button-binding fields.
    friend bool operator==(const ButtonDescriptor& left,
                           const ButtonDescriptor& right) noexcept
    {
        return left.action_id == right.action_id && left.payload_version == right.payload_version
            && left.payload == right.payload && left.validated == right.validated;
    }
};

/// \brief One value-owned node in a deterministic flattened UI tree.
struct UiNode {
    NodeKind kind; ///< Semantic node kind.
    std::string id; ///< Identifier unique among siblings.
    std::string path; ///< Deterministic identity path within the render.
    std::string text; ///< Visible text or heading/label content.
    std::optional<ButtonDescriptor> button; ///< Action binding for button nodes.
    std::vector<std::size_t> children; ///< Indices into the owning `UiTree` node vector.

    /// \brief Compares complete node value state.
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
    /// \brief Takes ownership of a supplied flattened node sequence.
    /// \param nodes Nodes supplied by the caller. The constructor does not
    ///        validate child-index ranges or other tree invariants.
    explicit UiTree(std::vector<UiNode> nodes) : m_nodes(std::move(nodes)) {}

    /// \brief Returns immutable flattened nodes for this render.
    /// \return Node sequence owned by this tree.
    [[nodiscard]] const std::vector<UiNode>& nodes() const noexcept
    {
        return m_nodes;
    }

    /// \brief Compares deterministic render results by complete node state.
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
    /// \brief Move-only RAII guard that closes one builder scope on destruction.
    class Scope {
    public:
        Scope() noexcept = default;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        /// \brief Transfers responsibility for closing an active scope.
        /// \param other Scope whose ownership is transferred.
        Scope(Scope&& other) noexcept;
        /// \brief Transfers scope-closing responsibility after closing this scope.
        /// \param other Scope whose ownership is transferred.
        /// \return This scope after the transfer.
        Scope& operator=(Scope&& other) noexcept;
        ~Scope();

        /// \brief Reports whether this object still owns an open builder scope.
        /// \return `true` until moved from or closed.
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

    /// \brief Creates a single-render builder optionally bound to an action registry.
    /// \param registry Borrowed registry used to validate typed button handles.
    explicit UiBuilder(ActionRegistry* registry = nullptr) : m_registry(registry) {}
    UiBuilder(const UiBuilder&) = delete;
    UiBuilder& operator=(const UiBuilder&) = delete;

    /// \brief Opens the required root scope.
    /// \param id Stable root identifier.
    /// \return RAII scope or a duplicate/state error.
    [[nodiscard]] Result<Scope> begin(std::string_view id);

    /// \brief Opens a nested section scope that closes at RAII scope end.
    /// \param id Identifier unique within the current parent.
    /// \param title Optional section heading.
    /// \return RAII scope or a duplicate/state error.
    [[nodiscard]] Result<Scope> section(std::string_view id, std::string_view title = {});

    /// \brief Opens a keyboard row/grouping scope.
    /// \param id Identifier unique within the current parent.
    /// \return RAII scope or a duplicate/state error.
    [[nodiscard]] Result<Scope> row(std::string_view id);

    /// \brief Appends a text node to the current scope.
    /// \param id Identifier unique within the current parent.
    /// \param value Visible text.
    /// \return Success or a duplicate/state error.
    [[nodiscard]] Result<void> text(std::string_view id, std::string_view value);
    /// \brief Appends a heading node to the current scope.
    /// \param id Identifier unique within the current parent.
    /// \param value Visible heading text.
    /// \return Success or a duplicate/state error.
    [[nodiscard]] Result<void> heading(std::string_view id, std::string_view value);
    /// \brief Appends a separator node to the current scope.
    /// \param id Identifier unique within the current parent.
    /// \return Success or a duplicate/state error.
    [[nodiscard]] Result<void> separator(std::string_view id);
    /// \brief Appends an unvalidated string action binding.
    /// \param id Identifier unique within the current parent.
    /// \param label Visible button label.
    /// \param action_id Canonical action route.
    /// \param payload Already encoded payload bytes.
    /// \return Success or a validation/state error.
    [[nodiscard]] Result<void> button(std::string_view id,
                                      std::string_view label,
                                      std::string_view action_id,
                                      std::string_view payload = {});

    template <typename Payload>
    /// \brief Appends a button after validating and encoding a typed action handle.
    /// \param id Identifier unique within the current parent.
    /// \param label Visible button label.
    /// \param action Typed action handle.
    /// \param payload Typed payload to encode.
    /// \return Success or a stale-handle, codec or builder-state error.
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
    /// \brief Compatibility overload accepting payload before the typed handle.
    /// \param id Identifier unique within the current parent.
    /// \param label Visible button label.
    /// \param payload Typed payload to encode.
    /// \param action Typed action handle.
    /// \return Success or a stale-handle, codec or builder-state error.
    [[nodiscard]] Result<void> button(std::string_view id,
                                      std::string_view label,
                                      const Payload& payload,
                                      const ActionHandle<Payload>& action)
    {
        return button(id, label, action, payload);
    }

    /// \brief Finishes a render after all RAII scopes have closed.
    /// \return Immutable tree or an unbalanced-scope/state error.
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
