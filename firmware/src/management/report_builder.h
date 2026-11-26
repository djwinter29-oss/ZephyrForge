/**
 * @file report_builder.h
 * @brief Assemble live management reports from their owning services.
 */
#ifndef ZEPHYRFORGE_MANAGEMENT_REPORT_BUILDER_H
#define ZEPHYRFORGE_MANAGEMENT_REPORT_BUILDER_H

#include <stdint.h>

/** Return the payload size for a report ID, or zero when it is not defined in this image. */
uint16_t zf_management_report_size(uint8_t id);

/**
 * @brief Build one shared report payload, excluding the transport envelope.
 * @param id ZF_REPORT_* identifier.
 * @param sequence Input-report sequence; ignored by reports without a sequence field.
 * @param out Destination sized for the selected report.
 * @return Payload size, or zero when the report is not built.
 */
uint16_t zf_management_report_build(uint8_t id, uint8_t sequence, uint8_t *out);

#endif
