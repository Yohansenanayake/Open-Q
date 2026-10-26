// Copyright (C) 2026 - zsliu98
// Adapted for Open-Q, 26 October 2026: renamed type/namespace, initialized slots, removed raw-array access.
// Original: ZLEqualizer 3ce598ee065b3d3847121e76e196feda7ed8f56d, source/panel/helper/tri_buffer.hpp.
// This file is part of ZLEqualizer
//
// ZLEqualizer is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLEqualizer is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLEqualizer. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace openq
{
    /**
     * a single-producer, single-consumer buffer retaining the latest publication
     */
    template <typename C>
    class TripleBuffer
    {
    public:
        TripleBuffer() = default;

        /**
         * pull new object before reading
         * @return whether the object has been updated
         */
        bool pull()
        {
            if ((middle_state_.load (std::memory_order::relaxed) & kDirtyBit) == 0)
            {
                return false;
            }
            // only the consumer clears the dirty bit; concurrent publishes keep it set
            read_idx_ = middle_state_.exchange (read_idx_, std::memory_order_acq_rel) & kIndexMask;
            return true;
        }

        /**
         * get the reader object
         * @return
         */
        const C& getReader() const
        {
            return buffer_[static_cast<std::size_t> (read_idx_)];
        }

        /**
         * get the writer object
         * @return
         */
        C& getWriter()
        {
            return buffer_[static_cast<std::size_t> (write_idx_)];
        }

        /**
         * publish new object after writing
         */
        void publish()
        {
            write_idx_ = middle_state_.exchange (write_idx_ | kDirtyBit, std::memory_order_acq_rel) & kIndexMask;
        }

    private:
        using State = std::uint32_t;
        static_assert (std::atomic<State>::is_always_lock_free, "TripleBuffer requires lock-free atomic std::uint32_t");

        static constexpr State kIndexMask = 0b011u;
        static constexpr State kDirtyBit = 0b100u;

        std::array<C, 3> buffer_ {};

        // Separate producer/consumer metadata on typical 64-byte cache lines.
        alignas (64) std::atomic<State> middle_state_ { 2 };

        alignas (64) State read_idx_ { 0 };
        alignas (64) State write_idx_ { 1 };
    };
}
