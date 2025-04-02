#ifndef RISCV_TYPE_H
#define RISCV_TYPE_H

#include <memory.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#define NR_REG 54

enum Engine {
	FENCE = 0b000000,
	MMA = 0b000001,
	SMX = 0b000010,
	ACT = 0b000011,
};

enum QueueState {
	EMPTY = 0,
	FULL = 1,
	PARTIAL = 2,
};

class BF16 {
   public:
	uint16_t value;  // 16 位存储

	BF16() : value(0) {}

	BF16(uint16_t raw) : value(raw) {}

	// 转换回float
	float toFP32() const {
		// 将16位扩展到32位（低16位补0）
		uint32_t floatBits = static_cast<uint32_t>(value) << 16;
		float result;
		memcpy(&result, &floatBits, sizeof(float));
		return result;
	}

	// 获取原始 16 位值
	uint16_t getRawValue() const {
		return value;
	}

	// 赋值运算符重载
	BF16& operator=(uint16_t value) {
		this->value = value;
		return *this;
	}

	// 输出运算符重载
	friend std::ostream& operator<<(std::ostream& os, const BF16& bf) {
		os << bf.value;
		return os;
	}
};

class FP8 {
   public:
	uint8_t value;  // 8 位存储

	FP8() : value(0) {}

	FP8(uint8_t raw) : value(raw) {}

	// 赋值运算符重载
	FP8& operator=(uint8_t value) {
		this->value = value;
		return *this;
	}

	// 输出运算符重载
	friend std::ostream& operator<<(std::ostream& os, const FP8& fp) {
		os << fp.value;
		return os;
	}

	// 转换为 FP32（float）
	float toFP32() const {
		// 提取符号位（1 位）
		uint8_t sign = (value >> 7) & 0x1;

		// 提取指数部分（4 位）
		uint8_t exponent = (value >> 3) & 0xF;

		// 提取尾数部分（3 位）
		uint8_t mantissa = value & 0x7;

		// 特殊情况处理：FP8 的零值
		if (exponent == 0 && mantissa == 0) {
			return sign == 1 ? -0.0f : 0.0f;  // 返回正零或负零
		}

		// FP8 的非零情况
		float signMultiplier = sign == 1 ? -1.0f : 1.0f;

		if (exponent == 0) {
			// 次正规值（Subnormal）：指数为 0，但有有效的尾数
			float subnormal = (mantissa / 8.0f) * std::pow(2, -6);  // 指数为 -6
			return signMultiplier * subnormal;
		} else if (exponent == 15) {
			// 特殊值（NaN 或无穷大）
			if (mantissa != 0) {
				return std::nanf("");  // NaN
			} else {
				return signMultiplier * std::numeric_limits<float>::infinity();  // Inf
			}
		} else {
			// 正常值（Normal）：计算值
			int adjustedExponent = static_cast<int>(exponent) - 7;  // 去掉偏移量
			float normal = (1.0f + mantissa / 8.0f) * std::pow(2, adjustedExponent);
			return signMultiplier * normal;
		}
	}

	// 获取原始 FP8 值
	uint8_t getRawValue() const {
		return value;
	}
};

class Fileds {
public:
    // 寄存器数组：通过序号访问寄存器名称
    std::string abiName[NR_REG] = {
        "zero",
        "opcode",
        "mma.opmask",
        "mma.hp.en",
        "mma.lp.en",
        "mma.n",
        "mma.k",
        "mma.m",
        "mma.hp.addr",
        "mma.hp.stride",
        "mma.hp.dtype",
        "mma.lp.addr",
        "mma.lp.stride",
        "mma.lp.dtype",
        "mma.w.addr",
        "mma.w.stride",
        "mma.w.dtype",
        "mma.mask.addr",
        "mma.mask.stride",
        "mma.scale.hp.addr",
        "mma.scale.hp.stride",
        "mma.scale.lp.addr",
        "mma.scale.lp.stride",
        "mma.scale.w.addr",
        "mma.scale.w.stride",
        "mma.acc.addr",
        "mma.acc.stride",
        "mma.out.addr",
        "mma.out.stride",
        "mma.out.dtype",
        "smx.opmask",
        "smx.max.val",
        "smx.m",
        "smx.n",
        "smx.dim",
        "smx.in.addr",
        "smx.in.stride",
        "smx.in.dtype",
        "smx.p.addr",
        "smx.p.stride",
        "smx.out.addr",
        "smx.out.stride",
        "smx.out.dtype",
        "smx.init",
        "act.opmask",
        "act.mode",
        "act.m",
        "act.n",
        "act.in.addr",
        "act.in.stride",
        "act.in.dtype",
        "act.out.addr",
        "act.out.stride",
        "act.out.dtype",
    };
    
    std::map<std::string, int> nameToIndex;
    uint32_t regs[NR_REG] = {0}; 


    uint32_t hp_dtype_size = 0;
    uint32_t lp_dtype_size = 0;
    uint32_t w_dtype_size  = 0;

public:
    Fileds() {
        for (int i = 0; i < NR_REG; ++i) {
            nameToIndex[abiName[i]] = i;
        }
    }

    std::string getRegisterName(int index) const {
        if (index < 0 || index >= NR_REG) {
            throw std::out_of_range("Invalid register index!");
        }
        return abiName[index];
    }

    int getRegisterIndex(const std::string& name) const {
        auto it = nameToIndex.find(name);
        if (it == nameToIndex.end()) {
            throw std::invalid_argument("Invalid register name!");
        }
        return it->second;
    }

    uint32_t& operator[](const std::string& name) {
        auto it = nameToIndex.find(name);
        if (it == nameToIndex.end()) {
            std::cout << "Stupid name: " << name << std::endl;
            throw std::invalid_argument("Invalid register name!");
        }
        return regs[it->second];
    }
};

class IDAGIExtension {
public:
    uint32_t regs[NR_REG];

    IDAGIExtension() {
        for (int i = 0; i < NR_REG; i ++) {
            regs[i] = 0;
        }
    }

    uint8_t* build_fileds() {
        Fileds* fileds = new Fileds;
        for (int i = 0; i < NR_REG; i ++) {
			std::string name = fileds->getRegisterName(i);
			if (name == "mma.hp.addr" || name == "mma.lp.addr" || name == "mma.w.addr" || name == "mma.acc.addr" || name == "mma.out.addr" ||
                name == "smx.in.addr" || name == "smx.p.addr" || name == "smx.out.addr" ||
                name == "act.in.addr" || name == "act.out.addr") {
				fileds->regs[i] = linearize(regs[i]);
			} else if (name == "mma.k" || name == "mma.m" ||
                       name == "smx.dim") {
                switch (regs[i]) {
                    case 0: fileds->regs[i] = 16; break;
                    case 1: fileds->regs[i] = 32; break;
                    case 2: fileds->regs[i] = 64; break;
                    case 3: fileds->regs[i] = 128; break;
                    default:
                        printf("Invalid Code");
                        assert(0);
                }
			} else if (name == "mma.hp.dtype" || name == "mma.lp.dtype" || name == "mma.w.dtype") {
                uint32_t dtype_size = 0;
				switch (regs[i]) {
					case 0: dtype_size = 1; break;
					case 1: dtype_size = 1; break;
					case 2: dtype_size = 1; break;
					case 3: dtype_size = 2; break;
					case 4: dtype_size = 2; break;
					default:
						printf("Invalid Code");
						assert(0);
                }

                if (name == "mma.hp.dtype") {
                    fileds->hp_dtype_size = dtype_size;
                } else if (name == "mma.lp.dtype") {
                    fileds->lp_dtype_size = dtype_size;
                } else if (name == "mma.w.dtype") {
                    fileds->w_dtype_size = dtype_size;
                }
				fileds->regs[i] = regs[i];
			} else {
				fileds->regs[i] = regs[i];
			}
        }
        return (uint8_t*)fileds;
    }

    uint32_t linearize(uint32_t entry_bank) {
        uint32_t entry_idx = (entry_bank >> 6) & 0xff;
        uint32_t bank_idx = (entry_bank & 0x3f);
        return (entry_idx * 64 * 64) + bank_idx * 64;
    }
};
#endif