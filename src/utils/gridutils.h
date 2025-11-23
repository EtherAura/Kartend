#ifndef GRIDUTILS_H
#define GRIDUTILS_H

class GridUtils
{
public:
    static inline int computeItemRow(int index, int gridWidth) {
        return (gridWidth > 0) ? (index / gridWidth) : 0;
    }

    static inline int computeItemCol(int index, int gridWidth) {
        return (gridWidth > 0) ? (index % gridWidth) : 0;
    }

    static inline int computeItemY(int index, int gridWidth, int itemHeight, int verticalSpacing, int margins) {
        int row = computeItemRow(index, gridWidth);
        return margins + row * (itemHeight + verticalSpacing);
    }

    static inline int computeItemX(int index, int gridWidth, int itemWidth, int horizontalSpacing, int margins) {
        int col = computeItemCol(index, gridWidth);
        return margins + col * (itemWidth + horizontalSpacing);
    }

    static inline int computeCenterTarget(int itemCoord, int itemSize, int viewportSize, int maxScroll) {
        int t = itemCoord + itemSize / 2 - viewportSize / 2;
        if (t < 0) t = 0;
        else if (t > maxScroll) t = maxScroll;
        return t;
}
    static inline void calculateGridMetrics(int totalItems, int itemsPerRow, int itemWidth, int itemHeight,
                                            int horizontalSpacing, int verticalSpacing, int margins,
                                            int& totalWidth, int& totalHeight, int& actualGridWidth)
                                            
    {
        int totalRows = (itemsPerRow > 0) ? ((totalItems + itemsPerRow - 1) / itemsPerRow) : 1;

        if (itemsPerRow > 0) {
            int horizontalSpacingContribution = (itemsPerRow > 1 ? (itemsPerRow - 1) * horizontalSpacing : 0);
            int calculatedWidth = margins * 2 + itemsPerRow * itemWidth + horizontalSpacingContribution;
            actualGridWidth = calculatedWidth;
            totalWidth = calculatedWidth;
        } else {
            totalWidth = margins * 2 + itemWidth;
            actualGridWidth = totalWidth;
        }

        int verticalSpacingContribution = (totalRows > 1 ? (totalRows - 1) * verticalSpacing : 0);
        totalHeight = margins + totalRows * itemHeight + verticalSpacingContribution;

        int minWidth = margins * 2 + itemWidth;
        if (totalWidth < minWidth) totalWidth = minWidth;

        int minHeight = margins + itemHeight;
        if (totalHeight < minHeight) totalHeight = minHeight;


    }
};

#endif