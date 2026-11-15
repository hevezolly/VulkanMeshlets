#pragma once
 
class CapacityController
{
    uint32_t m_capacity = 1024;
    uint32_t m_lowUsageFrames = 0;
    float m_growThreshold = 0.85f;
    float m_shrinkThreshold = 0.35f;
    uint32_t m_shrinkDelay = 120;
    uint32_t m_minCapacity = 1024;
    float m_excessFactor = 1.5;

public:

    uint32_t capacity() const {
        return m_capacity;
    }

    CapacityController() {}
    CapacityController(uint32_t minCapacity, float growThreshold, float shrinkThreshold, uint32_t shrinkDelay, float excessFactor): 
        m_capacity(minCapacity), 
        m_minCapacity(minCapacity), 
        m_growThreshold(growThreshold), 
        m_shrinkThreshold(shrinkThreshold),
        m_shrinkDelay(shrinkDelay),
        m_excessFactor(excessFactor)
    {} 

    void update(uint32_t recentPeak)
    {
        float recentPeakF = static_cast<float>(recentPeak);
        float capacityF = static_cast<float>(m_capacity);
        if (recentPeakF > capacityF * m_growThreshold)
        {
            m_lowUsageFrames = 0;

            m_capacity = std::ceil(recentPeak * 1.5);
        }
        else if (recentPeakF <
                 m_capacity * m_shrinkThreshold)
        {
            ++m_lowUsageFrames;

            if (m_lowUsageFrames >= m_shrinkDelay)
            {
                m_capacity = uint32_t(
                    std::ceil(float(recentPeak) * 1.5)
                );

                m_lowUsageFrames = 0;
            }
        }
        else
        {
            m_lowUsageFrames = 0;
        }
    }
};