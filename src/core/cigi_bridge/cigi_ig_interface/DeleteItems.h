#pragma once

template<typename T> bool DeleteAll(T theElement) { delete theElement; return true; }
template<typename TList> void ClearList(TList* list) { list->remove_if(DeleteAll<typename TList::value_type>); }

template<typename TList> void Delete(TList* list)
{
	ClearList(list);
	delete list;
}