#ifndef CZI_NANOPORE2_LONG_BASE_SEQUENCE_HPP
#define CZI_NANOPORE2_LONG_BASE_SEQUENCE_HPP

// Nanopore2.
#include "Base.hpp"
#include "MemoryMappedVectorOfVectors.hpp"

// Standard library.
#include "iostream.hpp"
#include "stdexcept.hpp"
#include "string.hpp"

namespace ChanZuckerberg {
    namespace Nanopore2 {

        // A long sequence of bases. Memory is not owned.
        class LongBaseSequenceView;
        inline ostream& operator<<(ostream&, const LongBaseSequenceView&);

        // A long sequence of bases. Memory is owned.
        class LongBaseSequence;

        // Many long sequences of bases
        class LongBaseSequences;

        // Reverse complement a vector of bases.
        inline void reverseComplement(vector<Base>&);

        void testLongBaseSequence();
    }
}



// Class that interprets a memory range as a set of uint64_t values,
// representing a long base sequence as follows:
// Position 0: LSB bit of bases 0-63   (with LSB bit for base 0  in MSB bit of this word)
// Position 1: MSB bit of bases 0-63   (with MSB bit for base 0  in MSB bit of this word)
// Position 2: LSB bit of bases 64-127 (with LSB bit for base 64 in MSB bit of this word)
// Position 3: MSB bit of bases 64-127 (with MSB bit for base 64 in MSB bit of this word)
// This choice of representation facilitates the implementation of various
// operations for high performance using low level bit manipulation.
// This class does not own the memory it manipulates.
class ChanZuckerberg::Nanopore2::LongBaseSequenceView {
public:
    uint64_t* begin;
    uint64_t baseCount;

    LongBaseSequenceView(
        uint64_t* begin,
        uint64_t baseCount) :
        begin(begin), baseCount(baseCount)
    {}

    LongBaseSequenceView() :
        begin(0), baseCount(0)
    {}

    // Return the base at a given position.
    Base operator[](uint64_t i) const
    {
        const uint64_t word0Index = (i >> 6ULL) << 1ULL;  // Because there are two words for each block of 64 bases.
        const uint64_t& word0 = begin[word0Index];
        const uint64_t word1Index = word0Index + 1ULL;
        const uint64_t& word1 = begin[word1Index];
        const uint64_t bitIndex = 63ULL - (i & 63ULL);
        const uint64_t bit0 = (word0 >> bitIndex) & 1ULL;
        const uint64_t bit1 = (word1 >> bitIndex) & 1ULL;
        const uint8_t value = uint8_t((bit1 << 1ULL) + bit0);
        return Base(value, Base::FromInteger());
    }

    // Set the base at a given position.
    void set(uint64_t i, Base base) {
        const uint64_t word0Index = (i >> 6ULL) << 1ULL;  // Because there are two words for each block of 64 bases.
        const uint64_t bitIndex = 63ULL - (i & 63ULL);
        const uint64_t mask = 1ULL << bitIndex;

        uint64_t& word0 = begin[word0Index];
        const uint64_t bit0 = (base.value) & 1ULL;
        if(bit0 == 0) {
            word0 &= ~mask;
        } else {
            word0 |= mask;
        }

        const uint64_t word1Index = word0Index + 1ULL;
        uint64_t& word1 = begin[word1Index];
        const uint64_t bit1 = (base.value >> 1ULL) & 1ULL;

        if(bit1 == 0) {
            word1 &= ~mask;
        } else {
            word1 |= mask;
        }

    }

    // Compute the number of uint64_t words given the number of bases.
    static uint64_t wordCount(uint64_t baseCount)
    {
        if(!baseCount) {
            return 0ULL;
        } else {
            return 2ULL * (((baseCount-1ULL) >> 6ULL) + 1ULL);
        }
    }

    // Write out, with optional reverse complement.
    ostream& write(ostream& s, bool reverseComplement = false) const
    {
        if(reverseComplement) {
            for(uint64_t i=baseCount-1; ; i--) {
                s << (*this)[i].complement();
                if(i == 0) {
                    break;
                }
            }
        } else {
            for(uint64_t i=0; i<baseCount; i++) {
                s << (*this)[i];
            }
        }
        return s;
    }

};



inline std::ostream& ChanZuckerberg::Nanopore2::operator<<(
    std::ostream& s,
    const ChanZuckerberg::Nanopore2::LongBaseSequenceView& sequence)
{
    return sequence.write(s);
}



// Class that uses a vector of uint64_t values
// to represent a sequence of bases as a LongBaseSequence.
class ChanZuckerberg::Nanopore2::LongBaseSequence : public  LongBaseSequenceView {
public:
    LongBaseSequence(uint64_t baseCountArgument = 0)
    {
        initialize(baseCountArgument);
    }
    void initialize(uint64_t baseCountArgument)
    {
        baseCount = baseCountArgument;
        data.resize(wordCount(baseCount));
        fill(data.begin(), data.end(), 0ULL);
        begin = data.data();
    }

    LongBaseSequence(const vector<Base>& s)
    {
        baseCount = s.size();
        data.resize(wordCount(baseCount));
        begin = data.data();
        for(size_t i=0; i<baseCount; i++) {
            set(i, s[i]);
        }
    }

private:
    vector<uint64_t> data;
    friend class LongBaseSequences;
};



// Many long sequences of bases stored in memory mapped files.
// This is used to store nanopore reads.
class ChanZuckerberg::Nanopore2::LongBaseSequences {
public:
    void createNew(const string& name, size_t pageSize);
    void accessExistingReadOnly(const string& name);
    void accessExistingReadWrite(const string& name);
    void accessExistingReadWriteOrCreateNew(const string& name, size_t pageSize);
    void remove();

    bool isOpen() const
    {
        return baseCount.isOpen && data.isOpen();
    }

    // Return a LongBaseSequenceView representing the i-th sequence stored.
    LongBaseSequenceView operator[](uint64_t i)
    {
        CZI_ASSERT(i < data.size());
        return LongBaseSequenceView(data[i].begin(), baseCount[i]);
    }

    uint64_t size() const
    {
        const uint64_t n = baseCount.size();
        CZI_ASSERT(data.size() == n);
        return n;
    }
    bool empty() const {
        return baseCount.size() == 0;
    }

    // Append a new sequence at the end.
    void append(const LongBaseSequence&);
    void append(const vector<Base>&);
    void append(size_t baseCount);

private:

    // The number of bases of each of the sequences.
    MemoryMapped::Vector<uint64_t> baseCount;

    // The data for all the sequences.
    MemoryMapped::VectorOfVectors<uint64_t, uint64_t> data;

};



// Reverse complement a vector of bases.
inline void ChanZuckerberg::Nanopore2::reverseComplement(vector<Base>&v)
{
    const size_t n = v.size();
    const size_t nm1 = n - 1;
    const size_t nHalf = n / 2;

    for(size_t i=0; i<nHalf; i++) {
        Base& x = v[i];
        Base& y = v[nm1 - i];
        const Base xRc = x.complement();
        const Base yRc = y.complement();
        x = yRc;
        y = xRc;
    }

    if((n % 2)) {
        v[nHalf].complementInPlace();
    }
}

#endif
