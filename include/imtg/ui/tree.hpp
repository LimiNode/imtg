#ifndef IMTG_UI_TREE_HPP
#define IMTG_UI_TREE_HPP

#include "imtg/core/result.hpp"

#include <cstddef>
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
    std::string payload;

    friend bool operator==(const ButtonDescriptor& left,
                           const ButtonDescriptor& right) noexcept
    {
        return left.action_id == right.action_id && left.payload == right.payload;
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

/// rief Immutable result of one deterministic UI render.
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

/// rief Builds one render without global state or shared mutable context.
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

    UiBuilder() = default;
    UiBuilder(const UiBuilder&) = delete;
    UiBuilder& operator=(const UiBuilder&) = delete;

    /// rief Opens the required root scope.
    [[nodiscard]] Result<Scope> begin(std::string_view id);

    /// rief Opens a nested section scope that closes at RAII scope end.
    [[nodiscard]] Result<Scope> section(std::string_view id, std::string_view title = {});

    /// rief Opens a keyboard row/grouping scope.
    [[nodiscard]] Result<Scope> row(std::string_view id);

    [[nodiscard]] Result<void> text(std::string_view id, std::string_view value);
    [[nodiscard]] Result<void> heading(std::string_view id, std::string_view value);
    [[nodiscard]] Result<void> separator(std::string_view id);
    [[nodiscard]] Result<void> button(std::string_view id,
                                      std::string_view label,
                                      std::string_view action_id,
                                      std::string_view payload = {});

    /// rief Finishes a render after all RAII scopes have closed.
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
};

} // namespace imtg

#endif // IMTG_UI_TREE_HPP
