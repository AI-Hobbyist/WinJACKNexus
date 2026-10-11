/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <stdint.h>

enum
{
    WJN_NODE_METADATA_VERSION = 4u,
    WJN_NODE_VIRTUAL_API_VERSION = 1u,
    WJN_NODE_JACK_NODE = 1u,
    WJN_NODE_VIRT_NODE = 2u,
    WJN_NODE_HAS_GUI = 1u,
    WJN_NODE_MODE_JACK = 1u,
    WJN_NODE_MODE_VIRT = 2u,
    WJN_NODE_PORT_INPUT = 1u,
    WJN_NODE_PORT_OUTPUT = 2u,
    WJN_NODE_PORT_AUDIO = 1u,
    WJN_NODE_PORT_MIDI = 2u
};

typedef struct WjnNodeMetadataV4
{
    uint32_t structSize;
    uint32_t version;
    uint32_t capabilities;
    uint32_t features;
    uint32_t reserved[2];
} WjnNodeMetadataV4;

/* Descriptions and UTF-8 names remain module-owned until instance destruction.
   Stable IDs identify instance-local ports; names are presentation only. */
typedef struct WjnNodePortV1
{
    uint32_t structSize;
    uint32_t id;
    uint32_t direction;
    uint32_t type;
    uint32_t channels;
    uint32_t reserved;
    const char *name;
} WjnNodePortV1;

typedef struct WjnNodePrepareV1
{
    uint32_t structSize;
    uint32_t maxFrames;
    double sampleRate;
    void *hostContext;
} WjnNodePrepareV1;

typedef struct WjnNodeAudioBufferV1
{
    uint32_t portId;
    uint32_t channels;
    float **samples;
} WjnNodeAudioBufferV1;

typedef struct WjnNodeMidiEventV1
{
    uint32_t frame;
    uint32_t size;
    const uint8_t *data;
} WjnNodeMidiEventV1;

typedef struct WjnNodeMidiInputV1
{
    uint32_t portId;
    uint32_t eventCount;
    const WjnNodeMidiEventV1 *events;
} WjnNodeMidiInputV1;

/* Host supplies bounded arrays and payload storage. Drop newest on overflow,
   increment dropped, and never write beyond either capacity. */
typedef struct WjnNodeMidiOutputV1
{
    uint32_t portId;
    uint32_t eventCapacity;
    WjnNodeMidiEventV1 *events;
    uint8_t *bytes;
    uint32_t byteCapacity;
    uint32_t eventCount;
    uint32_t byteCount;
    uint32_t reserved;
    uint64_t dropped;
} WjnNodeMidiOutputV1;

typedef struct WjnNodeProcessV1
{
    uint32_t structSize;
    uint32_t frames;
    uint64_t samplePosition;
    uint32_t audioInputCount;
    uint32_t audioOutputCount;
    const WjnNodeAudioBufferV1 *audioInputs;
    WjnNodeAudioBufferV1 *audioOutputs;
    uint32_t midiInputCount;
    uint32_t midiOutputCount;
    const WjnNodeMidiInputV1 *midiInputs;
    WjnNodeMidiOutputV1 *midiOutputs;
} WjnNodeProcessV1;

/* Instance argument is the factory result. All buffers are borrowed for this
   call only. The host serializes prepare/process/reset/stop/destruction. */
typedef struct WjnNodeVirtualApiV1
{
    uint32_t structSize;
    uint32_t version;
    uint32_t (*portCount)(void *node);
    int32_t (*port)(void *node, uint32_t index, WjnNodePortV1 *description);
    int32_t (*prepare)(void *node, const WjnNodePrepareV1 *context);
    void (*process)(void *node, WjnNodeProcessV1 *block);
    void (*reset)(void *node);
    void (*stop)(void *node);
} WjnNodeVirtualApiV1;
