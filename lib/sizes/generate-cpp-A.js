"use strict"
const fs = require("fs");
const { getMinBits, getMaxBits, readCsvFile } = require("./common")

const c_max_class_size_L2 = 32

function readSizeClasses() {
    const sizeClasses = readCsvFile("./size-classes-A.csv")
        .filter(entry => !!entry.algo)
    sizeClasses.push({
        algo: "unit",
        size: Math.pow(2, 32),
    })
    return sizeClasses
}

function computeSizeClassesMappings(sizeClasses) {

    function updateSizeClassesAlignment() {
        for (const szcls of sizeClasses) {
            szcls.alignL2 = getMinBits(szcls.size) - 1
        }
    }

    function computeSizeL2Mapping(sizeL2) {
        const min_size = sizeL2 ? Math.pow(2, sizeL2 - 1) : 0
        const max_size = Math.pow(2, sizeL2)

        // Compute size alignement for the mapping
        let szClsId = 0
        let sizeAlignL2 = c_max_class_size_L2
        for (let csize = min_size; csize < max_size; csize += 8) {
            while (sizeClasses[szClsId + 1] && sizeClasses[szClsId + 1].size < csize) szClsId++
            if (sizeClasses[szClsId].alignL2 < sizeAlignL2) {
                sizeAlignL2 = sizeClasses[szClsId].alignL2
            }
        }
        const sizeAlign = Math.pow(2, sizeAlignL2)

        // Compute class size mapping table
        const tableLen = sizeL2 > sizeAlignL2 ? Math.pow(2, sizeL2 - sizeAlignL2 - 1) : 1
        console.log("tableLen", tableLen, "sizeL2", sizeL2, "sizeAlignL2", sizeAlignL2)
        const table = new Array(tableLen)
        szClsId = 0
        for (let i = 0; i < tableLen; i++) {
            const csize = min_size + i * sizeAlign
            while (sizeClasses[szClsId + 1] && sizeClasses[szClsId + 1].size < csize) szClsId++
            table[i] = szClsId
        }

        return {
            size: max_size,
            sizeL2,
            sizeAlign,
            sizeAlignL2,
            table,
        }
    }

    updateSizeClassesAlignment()

    const binsL2 = []
    let binsCount = 0
    for (let sizeL2 = 0; sizeL2 < c_max_class_size_L2; sizeL2++) {
        const entry = computeSizeL2Mapping(sizeL2)
        binsL2.push(entry)
        binsCount += entry.table.length
    }
    return {
        binsL2,
        binsCount,
    }
}

function generateCpp() {
    const sizeClasses = readSizeClasses()
    const mapping = computeSizeClassesMappings(sizeClasses)

    // Generate size converter constants
    const mapping_classids = []
    const mapping_size_L2 = []
    let mapping_classids_length = 0
    for (const binL2 of mapping.binsL2) {
        const comment = `//--- size: ${binL2.sizeL2} bits`
        mapping_classids.push(comment, `   ${binL2.table.join(", ")},`)
        mapping_size_L2.push(`   { ${Math.pow(2, binL2.sizeAlignL2) - 1}, ${binL2.sizeAlignL2}, ${mapping_classids_length} }, ${comment}`)
        mapping_classids_length += binL2.table.length
    }

    // Generate classes initializers
    const classes_initializers = []
    for (let i = 0; i < sizeClasses.length; i++) {
        let decl = ""
        const cls = sizeClasses[i]
        if (cls.algo == "slabL2") {
            const page_sizeL2 = getMaxBits(cls.page_size)
            decl = `AsPageSlabL2(0, ${cls.size}, ${page_sizeL2})`
        }
        else if (cls.algo == "slab1357") {
            const page_sizeL2 = getMaxBits(cls.page_size)
            decl = `AsPageSlab1357(0, ${cls.size}, ${page_sizeL2})`
        }
        else if (cls.algo == "segment") {
            const multiplier = cls.page_size
            const lengthL2 = cls.page_size
            decl = `AsSegmentSpan(${multiplier}, ${lengthL2})`
        }
        else if (cls.algo == "unit") {
            decl = `AsUnitSpan()`
        }
        classes_initializers.push(`   /* block_size=${cls.size}, block_per_page=${cls.block_per_page}, page_size=${cls.page_size} */ classes[${i}].${decl};`)
    }

    // Generate c++ header
    const file_h = `#pragma once
#include "./memory-space.h"
#include "./memory-context.h"

namespace sat {

struct SizeClass {
   virtual address_t allocate(size_t target, MemoryContext* context) = 0;
   virtual size_t size_max() = 0;
};

struct SizeClassEntry : SizeClass {
   uint8_t data[8];
   virtual address_t allocate(size_t target, MemoryContext* context);
   virtual size_t size_max();
   void AsPageSlabL2(uint8_t object_binID, uint16_t object_size, uint8_t page_sizeL2);
   void AsPageSlab1357(uint8_t object_binID, uint16_t object_size, uint8_t page_sizeL2);
   void AsSegmentSpan(uint8_t multiplier, uint16_t lengthL2);
   void AsUnitSpan();
};

class SizeClassTable {
public:
   SizeClassEntry classes[${sizeClasses.length}];
   SizeClassTable();

   inline size_t getSizeClassID(size_t size) {
      auto sizeL2 = msb_32(size);
      auto& mapL2 = mapping_size_L2[sizeL2];
      auto mapIndex = (size + mapL2.offset) >> (sizeL2 - mapL2.shift);
      return mapping_classids[mapL2.startIndex + mapIndex];
   }

   inline SizeClass* getSizeClass(size_t size) {
      return &this->classes[this->getSizeClassID(size)];
   }

private:
   struct MapSizeL2_t {
      uint16_t offset;
      uint16_t shift;
      uint32_t startIndex;
   };
   static const uint8_t mapping_classids[${mapping_classids_length}];
   static const MapSizeL2_t mapping_size_L2[${c_max_class_size_L2}];
};

extern SizeClassTable sizeClassTable;

}

`
    const file_cpp = `
#include "./sizeclass.h"

using namespace sat;

SizeClassTable sat::sizeClassTable;

const uint8_t sat::SizeClassTable::mapping_classids[${mapping_classids_length}] = {
${mapping_classids.join("\n")}
};

const SizeClassTable::MapSizeL2_t sat::SizeClassTable::mapping_size_L2[${c_max_class_size_L2}] = {
${mapping_size_L2.join("\n")}
};

sat::SizeClassTable::SizeClassTable() {
${classes_initializers.join("\n")}
}

address_t sat::SizeClassEntry::allocate(size_t target, MemoryContext* context) {
   return 0;
}
size_t sat::SizeClassEntry::size_max() {
   return 0;
}

`
    fs.writeFileSync("../sizeclass.h", file_h)
    fs.writeFileSync("../sizeclass.cpp", file_cpp)
}

generateCpp()
/*
for (let i = 0; i < 128; i += 8) {
    console.log(`${i}: ${getMinBits(i)} bits`)
}
*/