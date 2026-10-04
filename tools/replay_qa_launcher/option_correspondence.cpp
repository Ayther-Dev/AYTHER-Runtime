#include "option_correspondence.h"

#include "check_option_descriptors.h"

#if defined(AYTHER_REPLAY_QA_HAS_RUNTIME_VERSION)
#include "ayther_runtime_version.h"
#endif

#include <toml++/toml.hpp>

#include <sstream>
#include <vector>

namespace ayther::replay_qa_launcher {
namespace {

struct ReferenceOption {
    std::string command;
    std::string flag;
    bool required{};
};

struct ReferenceInventory {
    std::string source;
    std::string ref;
    std::string commit;
    std::vector<ReferenceOption> options;
};

std::string_view delivered_version() noexcept {
#if defined(AYTHER_REPLAY_QA_HAS_RUNTIME_VERSION)
    return runtime::runtime_version;
#else
    return "unknown";
#endif
}

bool read_inventory(std::string_view text, ReferenceInventory &inventory) {
    try {
        const auto table = toml::parse(text);
        inventory.source = table["source"].value_or(std::string{});
        inventory.ref = table["ref"].value_or(std::string{});
        inventory.commit = table["commit"].value_or(std::string{});
        const auto *options = table["option"].as_array();
        if (options == nullptr)
            return false;
        for (const auto &node : *options) {
            const auto *option = node.as_table();
            if (option == nullptr)
                return false;
            ReferenceOption item{(*option)["command"].value_or(std::string{}),
                                 (*option)["flag"].value_or(std::string{}),
                                 (*option)["required"].value_or(false)};
            if (item.command.empty() || item.flag.empty())
                return false;
            inventory.options.push_back(std::move(item));
        }
        return true;
    } catch (const toml::parse_error &) {
        return false;
    }
}

std::string_view obligation(bool required) noexcept {
    return required ? "obligatoria" : "opcional";
}

std::string_view correspondence_text(audio_qa::ReferenceCorrespondence value) noexcept {
    switch (value) {
    case audio_qa::ReferenceCorrespondence::same:
        return "misma";
    case audio_qa::ReferenceCorrespondence::changed:
        return "cambiada";
    case audio_qa::ReferenceCorrespondence::added:
        return "añadida";
    }
    return "—";
}

const audio_qa::FixedOptionDescriptor *find_fixed(std::string_view command,
                                                  std::string_view flag) noexcept {
    for (const auto &option : audio_qa::fixed_option_descriptors())
        if (option.command == command && option.flag == flag)
            return &option;
    return nullptr;
}

bool in_reference(const ReferenceInventory &inventory, std::string_view flag) {
    for (const auto &option : inventory.options)
        if (option.command == "check" && option.flag == flag)
            return true;
    return false;
}

void check_row(std::ostringstream &output, const audio_qa::CheckOptionDescriptor &option,
               std::string_view reference) {
    output << "| `" << option.flag << "` | " << reference << " | " << obligation(option.required)
           << (option.repeatable ? ", repetible" : "") << " | `" << option.launcher_field << "` | "
           << correspondence_text(option.correspondence) << " | "
           << (option.requirements.empty() ? std::string_view{"—"} : option.requirements) << " |\n";
}

} // namespace

std::variant<std::string, CorrespondenceError>
format_option_correspondence(std::string_view reference_inventory) {
    ReferenceInventory inventory;
    if (!read_inventory(reference_inventory, inventory))
        return CorrespondenceError{{}, "invalid_inventory"};

    for (const auto &option : inventory.options) {
        if (option.command == "check") {
            const auto *delivered = audio_qa::find_check_option(option.flag);
            if (delivered == nullptr)
                return CorrespondenceError{option.flag, "missing_in_delivered_table"};
            if (delivered->correspondence != audio_qa::ReferenceCorrespondence::same &&
                delivered->requirements.empty())
                return CorrespondenceError{option.flag, "change_without_requirement"};
            if (delivered->correspondence == audio_qa::ReferenceCorrespondence::same &&
                delivered->required != option.required)
                return CorrespondenceError{option.flag, "inconsistent_correspondence"};
        } else {
            const auto *delivered = find_fixed(option.command, option.flag);
            if (delivered == nullptr)
                return CorrespondenceError{option.flag, "missing_in_delivered_table"};
            if (delivered->required != option.required)
                return CorrespondenceError{option.flag, "inconsistent_correspondence"};
        }
    }
    for (const auto &option : audio_qa::check_option_descriptors()) {
        if (option.launcher_field.empty())
            return CorrespondenceError{std::string{option.flag}, "missing_launcher_field"};
        const bool reference = in_reference(inventory, option.flag);
        if (reference == (option.correspondence == audio_qa::ReferenceCorrespondence::added))
            return CorrespondenceError{std::string{option.flag}, "inconsistent_correspondence"};
        if (!reference && option.requirements.empty())
            return CorrespondenceError{std::string{option.flag}, "addition_without_requirement"};
    }

    std::ostringstream output;
    output << "# Correspondencia de opciones de replay (RF-1.8)\n\n"
           << "Generado por la prueba `replay_qa_launcher_option_coverage` (BR-171). "
              "No se edita a mano.\n\n"
           << "- **Versión de partida:** `" << inventory.source << "` `" << inventory.ref
           << "`, commit `" << inventory.commit
           << "`, inventario congelado `evidence/rf1-option-inventory-beta8.toml` (BR-004).\n"
           << "- **Versión entregada:** tabla `check_option_descriptors` de `ayther_audio_qa`, "
              "compilada con Runtime `"
           << delivered_version() << "`; el launcher `ayther_replay_qa` deriva un campo de "
           << "cada descriptor.\n\n"
           << "## `check`: opciones de replay y campos del launcher\n\n"
           << "| Opción | Referencia | Entregada | Campo del launcher | Correspondencia | "
              "Requisitos |\n"
           << "|---|---|---|---|---|---|\n";
    for (const auto &option : inventory.options)
        if (option.command == "check")
            check_row(output, *audio_qa::find_check_option(option.flag),
                      obligation(option.required));
    for (const auto &option : audio_qa::check_option_descriptors())
        if (!in_reference(inventory, option.flag))
            check_row(output, option, "—");

    output << "\n## `query` y `audit`: sin cambios\n\n"
           << "No son opciones de replay ni tienen campo en el launcher; se conservan "
              "como en la referencia.\n\n"
           << "| Subcomando | Opción | Referencia | Entregada | Correspondencia |\n"
           << "|---|---|---|---|---|\n";
    for (const auto &option : inventory.options) {
        if (option.command == "check")
            continue;
        const auto *delivered = find_fixed(option.command, option.flag);
        output << "| `" << option.command << "` | `" << option.flag << "` | "
               << obligation(option.required) << " | " << obligation(delivered->required)
               << " | misma |\n";
    }
    output << "\nResultado: ninguna opción de la referencia queda sin correspondencia.\n";
    return output.str();
}

} // namespace ayther::replay_qa_launcher
