///////////////////////////////////////////////////////////////////////////////
// FILE : font.cpp
// Machine generated IDispatch wrapper class(es) created by Microsoft Visual C++
// COleFont wrapper class implementation
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "font.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

///////////////////////////////////////////////////////////////////////////////
// COleFont properties
///////////////////////////////////////////////////////////////////////////////
CString COleFont::GetName()
{
	CString result;
	InvokeHelper(DISPID_NAME, DISPATCH_PROPERTYGET, VT_BSTR, (void*)&result, NULL);
	return result;
}

void COleFont::SetName(LPCTSTR lpszNewValue)
{
	static BYTE parms[] = VTS_BSTR;
	InvokeHelper(DISPID_NAME, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, lpszNewValue);
}

CY COleFont::GetSize()
{
	CY result;
	InvokeHelper(DISPID_FONTSIZE, DISPATCH_PROPERTYGET, VT_CY, (void*)&result, NULL);
	return result;
}

void COleFont::SetSize(const CY& newValue)
{
	static BYTE parms[] = VTS_CY;
	InvokeHelper(DISPID_FONTSIZE, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, &newValue);
}

BOOL COleFont::GetBold()
{
	BOOL result;
	InvokeHelper(DISPID_BOLD, DISPATCH_PROPERTYGET, VT_BOOL, (void*)&result, NULL);
	return result;
}

void COleFont::SetBold(BOOL newValue)
{
	static BYTE parms[] = VTS_BOOL;
	InvokeHelper(DISPID_BOLD, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, newValue);
}

BOOL COleFont::GetItalic()
{
	BOOL result;
	InvokeHelper(DISPID_ITALIC, DISPATCH_PROPERTYGET, VT_BOOL, (void*)&result, NULL);
	return result;
}

void COleFont::SetItalic(BOOL newValue)
{
	static BYTE parms[] = VTS_BOOL;
	InvokeHelper(DISPID_ITALIC, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, newValue);
}

BOOL COleFont::GetUnderline()
{
	BOOL result;
	InvokeHelper(DISPID_UNDERLINE, DISPATCH_PROPERTYGET, VT_BOOL, (void*)&result, NULL);
	return result;
}

void COleFont::SetUnderline(BOOL newValue)
{
	static BYTE parms[] = VTS_BOOL;
	InvokeHelper(DISPID_UNDERLINE, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, newValue);
}

BOOL COleFont::GetStrikethrough()
{
	BOOL result;
	InvokeHelper(DISPID_STRIKETHROUGH, DISPATCH_PROPERTYGET, VT_BOOL, (void*)&result, NULL);
	return result;
}

void COleFont::SetStrikethrough(BOOL newValue)
{
	static BYTE parms[] = VTS_BOOL;
	InvokeHelper(DISPID_STRIKETHROUGH, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, newValue);
}

short COleFont::GetWeight()
{
	short result;
	InvokeHelper(DISPID_WEIGHT, DISPATCH_PROPERTYGET, VT_I2, (void*)&result, NULL);
	return result;
}

void COleFont::SetWeight(short newValue)
{
	static BYTE parms[] = VTS_I2;
	InvokeHelper(DISPID_WEIGHT, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, newValue);
}

short COleFont::GetCharset()
{
	short result;
	InvokeHelper(DISPID_CHARSET, DISPATCH_PROPERTYGET, VT_I2, (void*)&result, NULL);
	return result;
}

void COleFont::SetCharset(short newValue)
{
	static BYTE parms[] = VTS_I2;
	InvokeHelper(DISPID_CHARSET, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parms, newValue);
}
