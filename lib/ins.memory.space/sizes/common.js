const fs = require("fs");

module.exports = {
    getMinBits(value) {
        let val = 1
        for (let n = 0; n < 32; n++) {
            if (value & val) return n
            val *= 2
        }
        return 0
    },
    getMaxBits(value) {
        let val = 1
        for (let n = 0; n < 32; n++) {
            if (value < val) return n - 1
            val *= 2
        }
        throw new Error()
    },
    readCsvFile(path) {
        const csv = fs.readFileSync(path).toString()
        const lines = csv.replace(/\r/g, "").split("\n")
        const keys = lines.shift().split(";")
        return lines.map(line => {
            const values = line.split(";")
            const entry = {}
            for (const key of keys) {
                let value = values.shift()
                if (Number.parseInt(value)) value = Number.parseInt(value)
                entry[key] = value
            }
            return entry
        })
    }
}
